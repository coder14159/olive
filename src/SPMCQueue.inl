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

  /*
   * TODO: Use cases to consider
   * Probably remove cache functionality, uncomplex
   * - data size is smaller than header size
   * - cache size is smaller header size
   */
  return pop_from_cache (header, data);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header, class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop_from_cache (
  Header     &header,
  BufferType &data)
{
  /*
   * Use client local cache so consuming data in larger chunks
   *
   * TODO: Consider removing consumer cache functionality.
   * Doesn't seem to improve performance and adds complexity.
   */
  if (m_cacheEnabled)
  {
    if (m_queue->producer_restarted (m_consumer))
    {
      BOOST_LOG_TRIVIAL (info)
        << "Producer restarted. Reset the consumer prefetch cache.";
      m_cache.clear ();
    }

    /*
      * Nothing to do if both cache and queue are empty
      * If the cache is used it should contain both the header and the data
      */
    if (m_cache.size () < sizeof (Header))
    {
      /*
        * Check that header and payload are available
        */
      if (read_available () < sizeof (Header))
      {
        return false;
      }

      /*
       * Copy data from the shared queue to the local cache.
       * It can then be returned from the cache
       */
      if (!m_queue->pop (m_cache, m_producer, m_consumer, m_buffer))
      {
        return false;
      }
    }

    m_cache.pop (header);

    if (m_cache.capacity () < (header.size + sizeof (Header)))
    {
      /*
        * Disable the cache if a message received is too large to fit
        */
      BOOST_LOG_TRIVIAL(info) << "Disable the prefetch cache. "
                        << "Message size is too large (" << header.size << ")";

      m_cacheEnabled = false;

      m_cache.pop (data, std::min (header.size, m_cache.size ()));

      return m_queue->pop (data, header.size - data.size (),
                          m_producer, m_consumer, m_buffer);
    }
    /*
     * Make sure the payload is received, waiting if necessary
     * TODO Consider adding resilience if the payload is not sent.
     */
    while (m_cache.size () < header.size)
    {
      m_queue->pop (m_cache, m_producer, m_consumer, m_buffer);
    }

    return m_cache.pop (data, header.size);
  }

  UNREACHABLE ("Cached data retrieval failed");
}

} // namespace spmc {
