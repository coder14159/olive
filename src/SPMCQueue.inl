#include "Assert.h"

#include <boost/log/trivial.hpp>

#include <cmath>

namespace spmc {

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_queue (std::make_unique<QueueType> (capacity))
{
  ASSERT (m_queue.get () != nullptr, "SPMCQueue initialisation failed");
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

  BOOST_LOG_TRIVIAL(info) << "find or construct shared memory object: "
    << queueName << " in named shared memory: " << memoryName;

  m_queue = m_memory.find_or_construct<QueueType> (queueName.c_str())
                                                  (capacity, allocator);
  ASSERT_SS (m_queue != nullptr,
             "shared memory object initialisation failed: " << queueName);
}

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  const std::string &memoryName,
  const std::string &queueName)
  : m_memory (boost::interprocess::open_only, memoryName.c_str ())
{
  namespace bi = boost::interprocess;

  BOOST_LOG_TRIVIAL(info) << "find shared memory object: " << queueName
                          << " in named shared memory: " << memoryName;

  auto memory = m_memory.find<QueueType> (queueName.c_str());

  m_queue = memory.first;
  ASSERT_SS (m_queue != nullptr,
             "shared memory object initialisation failed: " << queueName);

  // check we have a single queue object, not an array of them
  ASSERT_SS (memory.second == 1,
             "queue object: " << queueName << " should not be an array");
}

template <class Allocator, size_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::read_available () const
{
  return (m_queue->committed () - m_consumer.consumed ());
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::cache_size (size_t size)
{
  m_cache.capacity (size);

  m_cacheEnabled = true;
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header               &header,
  const std::vector<uint8_t> &data)
{
  return m_queue->push (header, data);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header, class Data>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header  &header,
  const Data    &data)
{
  return m_queue->push (header, data);
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
template <class Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header               &header,
  std::vector<uint8_t> &data)
{
  /*
   * Use of the cache is only permnitted if the there are no consumers permitted
   * to drop messages.
   */
  if (m_cacheEnabled && !m_consumer.message_drops_allowed ())
  {
    /*
     * Use cases to consider
     * - data size is smaller than header size
     * - cache size is smaller header size
     *
     */
    if (m_queue->producer_restarted (m_consumer))
    {
      BOOST_LOG_TRIVIAL(info)
        << "Producer restarted. Clear the consumer prefetch cache.";
      m_cache.clear ();
    }

    /*
     * Nothing to do if both cache and queue are empty
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

      if (!m_queue->pop (m_cache, m_producer, m_consumer))
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
                           m_producer, m_consumer);
    }

    /*
     * Make sure the payload is received
     * TODO Consider adding resilience if the payload is not sent.
     */
    while (m_cache.size () < header.size)
    {
      m_queue->pop (m_cache, m_producer, m_consumer);
    }

    return m_cache.pop (data, header.size);
  }
  else
  {
    return m_queue->pop (header, data, m_producer, m_consumer);
  }

  ASSERT (false, "SPMCQueue::pop () should have returned");
}

} // namespace spmc {
