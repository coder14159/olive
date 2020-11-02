#include "Assert.h"

#include <boost/log/trivial.hpp>

#include <cmath>

namespace spmc {

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_queue (std::make_unique<QueueType> (capacity))
{
  ASSERT (m_queue.get () != nullptr, "In-process SPMCQueue initialisation failed");

  m_buffer = m_queue->buffer ();
}

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  const std::string &memoryName,
  const std::string &queueName,
  size_t capacity)
  : m_memory (boost::interprocess::open_only, memoryName.c_str ())
{
  namespace bi = boost::interprocess;

  SharedMemory::Allocator allocator (m_memory.get_segment_manager ());

  BOOST_LOG_TRIVIAL(info) << "Find or construct shared memory object: "
    << queueName << " in named shared memory: " << memoryName;

  m_queue = m_memory.find_or_construct<QueueType> (queueName.c_str())
                                                  (capacity, allocator);
  ASSERT_SS (m_queue != nullptr,
             "Shared memory object initialisation failed: " << queueName);

  BOOST_LOG_TRIVIAL(info) << "Found or created queue named '"
    << queueName << "' with capacity of " << m_queue->capacity () << " bytes";

  m_buffer = m_queue->buffer ();
}

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  const std::string &memoryName,
  const std::string &queueName)
  : m_memory (boost::interprocess::open_only, memoryName.c_str ())
{
  namespace bi = boost::interprocess;

  BOOST_LOG_TRIVIAL(info) << "Find shared memory object: " << queueName
                          << " in named shared memory: " << memoryName;

  auto memory = m_memory.find<QueueType> (queueName.c_str());

  m_queue = memory.first;

  ASSERT_SS (m_queue != nullptr,
             "Shared memory object initialisation failed: " << queueName);

  /*
   * check we have a single queue object, not an array of them
   */
  ASSERT_SS (memory.second == 1,
             "Queue object: " << queueName << " should not be an array");

  m_buffer = m_queue->buffer ();
}

template <class Allocator, size_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::capacity () const
{
  return m_queue->capacity ();
}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::empty () const
{
  return (read_available () == 0);
}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::message_drops_allowed () const
{
  return m_consumer.message_drops_allowed ();
}

// TODO: read_available should include data stored in the cache
template <class Allocator, size_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::read_available () const
{
  if (m_consumer.consumed () != Consumer::UnInitialised)
  {
    return (m_queue->committed () - m_consumer.consumed () + m_cache.size ());
  }

  return std::min (m_queue->committed (), m_queue->capacity ()) + m_cache.size ();
}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::cache_enabled () const
{
  return m_cacheEnabled;
}

template <class Allocator, size_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::cache_capacity () const
{
  return m_cache.capacity ();
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::resize_cache (size_t size)
{
  ASSERT_SS (size > sizeof (Header),
    "Cache size must be larger than size of Header: " << sizeof (Header));

  if (m_cache.capacity () != size)
  {
    m_cache.resize (size);

    m_cacheEnabled = true;
  }
}

template <class Allocator, size_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::cache_size () const
{
  return m_cache.size ();
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header               &header,
  const std::vector<uint8_t> &data)
{
  return m_queue->push (header, data, m_buffer);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header, class Data>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header  &header,
  const Data    &data)
{
  return m_queue->push (header, data, m_buffer);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (const Header &header)
{
  return m_queue->push (header, m_buffer);
}


template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::allow_message_drops ()
{
  m_consumer.allow_message_drops ();
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer ()
{
  m_queue->unregister_consumer (m_producer.index ());
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header, class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header     &header,
  BufferType &data)
{
  /*
   * The local data cache is only permitted if this consumer is not permitted to
   * drop messages.
   */
  if (!m_cacheEnabled || m_consumer.message_drops_allowed ())
  {
    return m_queue->pop (header, data, m_producer, m_consumer, m_buffer);
  }

  return pop_from_cache (header, data);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header, class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop_from_cache (
  Header     &header,
  BufferType &data)
{

  if (!m_cacheEnabled)
  {
    return false;
  }

  if (m_queue->producer_restarted (m_consumer) && !m_cache.empty ())
  {
    BOOST_LOG_TRIVIAL (info)
      << "Producer restarted. Clear the consumer prefetch cache.";
    m_cache.clear ();
  }

  if (m_cache.size () < sizeof (Header))
  {
    /*
     * Move a chunk of data from the shared queue into the local cache.
     *
     * The cache will be increased in size if it is too small.
     */
    m_queue->prefetch_to_cache (m_cache, m_producer, m_consumer, m_buffer);
  }

  if (m_cache.pop (header))
  {
    if (m_cache.capacity () < (header.size))
    {
      /*
       * If a message received is too large to fit in the cache, drain the
       * cache and disable local caching.
       */
      BOOST_LOG_TRIVIAL(warning)
        << "Disable the prefetch cache (" << m_cache.capacity () << " bytes), "
        << "message size is too large (" << header.size << " bytes).";

      m_cacheEnabled = false;

      m_cache.pop (data, m_cache.size ());

      std::vector<uint8_t> tmp (header.size);

      assert (m_queue->pop (tmp, header.size - data.size (),
                            m_producer, m_consumer, m_buffer));

      data.insert (data.end (), tmp.begin (), tmp.end ());

      return true;
    }

    /*
     * Make sure the payload is received, waiting if necessary
     *
     * TODO: exit this loop if the producer exits before the payload is sent
     */
    while (m_cache.size () < header.size)
    {
      m_queue->prefetch_to_cache (m_cache, m_producer, m_consumer, m_buffer);
    }

    return m_cache.pop (data, header.size);
  }

  return false;
}

} // namespace spmc {
