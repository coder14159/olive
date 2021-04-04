#include "Assert.h"
#include "detail/GetSize.h"

#include <boost/log/trivial.hpp>

#include <cmath>

namespace spmc {

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_queue (std::make_unique<QueueType> (capacity))
{
  ASSERT (m_queue.get () != nullptr,
        "In-process SPMCQueue initialisation failed");

  ASSERT (capacity > sizeof (Header),
        "SPMCQueue capacity must be greater than header size");
}

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  const std::string &memoryName,
  const std::string &queueName,
  size_t capacity)
: m_memory (boost::interprocess::open_only, memoryName.c_str ())
{
  ASSERT (capacity > sizeof (Header),
        "SPMCQueue capacity must be greater than header size");

  namespace bi = boost::interprocess;

  SharedMemory::Allocator allocator (m_memory.get_segment_manager ());

  BOOST_LOG_TRIVIAL(info) << "Find or construct shared memory object: "
    << queueName << " in named shared memory: " << memoryName;

  m_queue = m_memory.find_or_construct<QueueType> (queueName.c_str())
                                                  (capacity + 1, allocator);
  ASSERT_SS (m_queue != nullptr,
             "Shared memory object initialisation failed: " << queueName);
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
}

template <class Allocator, size_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::capacity () const
{
  return m_queue->capacity ();
}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::empty (
  detail::ConsumerState &consumer) const
{
  return (read_available (consumer) == 0);
}

template <class Allocator, size_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::read_available (
  detail::ConsumerState &consumer) const
{
  return m_queue->read_available (consumer) + m_cache.size ();
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
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (const Header &header)
{
  static_assert (std::is_trivially_copyable<Header>::value,
                "Header type must be trivially copyable");

  return m_queue->push (header);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header, class Data>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header  &header,
  const Data    &data)
{
  static_assert (std::is_trivially_copyable<Header>::value,
                "Header type must be trivially copyable");
  static_assert (std::is_trivially_copyable<Data>::value,
                "Data type must be trivially copyable");

  return m_queue->push_variadic (header, data);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header &header,
  const std::vector<uint8_t> &data)
{
  static_assert (std::is_trivially_copyable<Header>::value,
                "Header type must be trivially copyable");

  return m_queue->push_variadic (header, data);
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::register_consumer (
  detail::ConsumerState &consumer)
{
  m_queue->consumer_checks (consumer);
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  detail::ConsumerState &consumer)
{
  m_queue->unregister_consumer (consumer);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header, class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header     &header,
  BufferType &data,
  detail::ConsumerState &consumer)
{
  /*
   * The local data cache is only permitted if this consumer is not permitted to
   * drop messages.
   */
  // if (SPMC_EXPECT_TRUE (!m_cacheEnabled))
  {
    if (SPMC_EXPECT_TRUE (m_queue->pop (header, consumer) &&
                          header.type != WARMUP_MESSAGE_TYPE))
    {
      /*
       * If no message drops are permitted for the consumer then both header and
       * data are available as they are pushed as one atomic unit.
       *
       * If dropping of messages by the client is permitted then failure to pop
       * payload indicates the data has been overwritten in the shared data
       * queue.
       */
      data.resize (header.size);
      return m_queue->pop (data.data (), header.size, consumer);
    }
    else
    {
      return false;
    }
  }

  return pop_from_cache (header, data);
}

#if TODO // Cache functionality should work
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
#if TODO // server restart is removed
  if (m_queue->producer_restarted (m_consumer) && !m_cache.empty ())
  {
    BOOST_LOG_TRIVIAL (info)
      << "Producer restarted. Clear the consumer prefetch cache.";
    m_cache.clear ();
  }
#endif
  if (m_cache.size () < sizeof (Header))
  {
    /*
     * Move a chunk of data from the shared queue into the local cache.
     *
     * The cache will be increased in size if it is too small.
     */
    m_queue->prefetch_to_cache (m_cache, m_consumer);
  }

  if (m_cache.pop (header) && header.type != WARMUP_MESSAGE_TYPE)
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

      std::vector<uint8_t> tmp (header.size - data.size ());

      assert (m_queue->pop (tmp.data (), header.size - data.size (),
                            m_consumer));

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
      m_queue->prefetch_to_cache (m_cache, m_consumer);
    }

    return m_cache.pop (data, header.size);
  }

  return false;
}
#endif
} // namespace spmc {
