#include "Assert.h"
#include "detail/GetSize.h"

#include <boost/log/trivial.hpp>

#include <cmath>

namespace spmc {

template <class Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_queue (std::make_unique<QueueType> (capacity))
{
  CHECK (m_queue.get () != nullptr,
        "In-process SPMCQueue initialisation failed");

  CHECK (capacity > sizeof (Header),
        "SPMCQueue capacity must be greater than header size");

  // m_queue->register_producer ();
}

template <class Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  const std::string &memoryName,
  const std::string &queueName,
  size_t capacity)
: m_memory (boost::interprocess::open_only, memoryName.c_str ())
{
  CHECK (capacity > sizeof (Header),
        "SPMCQueue capacity must be greater than header size");

  namespace bi = boost::interprocess;

  SharedMemory::Allocator allocator (m_memory.get_segment_manager ());

  BOOST_LOG_TRIVIAL(info) << "Find or construct shared memory object: "
    << queueName << " in named shared memory: " << memoryName;

  m_queue = m_memory.find_or_construct<QueueType> (queueName.c_str())
                                                  (capacity, allocator);
  CHECK_SS (m_queue != nullptr,
             "Shared memory object initialisation failed: " << queueName);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
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

  CHECK_SS (m_queue != nullptr,
             "Shared memory object initialisation failed: " << queueName);

  /*
   * check we have a single queue object, not an array of them
   */
  CHECK_SS (memory.second == 1,
             "Queue object: " << queueName << " should not be an array");
}

template <class Allocator, uint8_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::capacity () const
{
  return m_queue->capacity ();
}

template <class Allocator, uint8_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::empty (
  detail::ConsumerState &consumer) const
{
  return (read_available (consumer) == 0);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::read_available (
  detail::ConsumerState &consumer) const
{
  return m_queue->read_available (consumer) + m_cache.size ();
}

template <class Allocator, uint8_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::write_available () const
{
  return m_queue->back_pressure ().write_available ();
}

template <class Allocator, uint8_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::cache_enabled () const
{
  return m_cacheEnabled;
}

template <class Allocator, uint8_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::cache_capacity () const
{
  return m_cache.capacity ();
}

template <class Allocator, uint8_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::resize_cache (size_t size)
{
  CHECK_SS (size > sizeof (Header),
    "Cache size must be larger than size of Header: " << sizeof (Header));

  if (m_cache.capacity () != size)
  {
    m_cache.capacity (size);

    m_cacheEnabled = true;
  }
}

template <class Allocator, uint8_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::cache_size () const
{
  return m_cache.size ();
}

template <class Allocator, uint8_t MaxNoDropConsumers>
template <class POD>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (const POD &pod)
{
  static_assert (std::is_trivially_copyable<Header>::value,
                "Header type must be trivially copyable");

  return m_queue->push (pod);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
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

template <class Allocator, uint8_t MaxNoDropConsumers>
template <class Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header &header,
  const std::vector<uint8_t> &data)
{
  static_assert (std::is_trivially_copyable<Header>::value,
                "Header type must be trivially copyable");

  return m_queue->push_variadic (header, data);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::register_consumer (
  detail::ConsumerState &consumer)
{
  m_queue->consumer_checks (consumer);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  detail::ConsumerState &consumer)
{
  m_queue->unregister_consumer (consumer);
}
// #define OLD_IMPL
#ifdef OLD_IMPL
template <class Allocator, uint8_t MaxNoDropConsumers>
template <class Header, class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header     &header,
  BufferType &data,
  detail::ConsumerState &consumer)
{
  if (SPMC_EXPECT_TRUE (!m_cacheEnabled))
  {
    /*
     * Test caching all available consumer data
     */
    if (m_queue->pop (header, consumer))
    {
      if (header.type == WARMUP_MESSAGE_TYPE)
      {
        return false;
      }

      data.resize (header.size);

      m_queue->pop (data.data (), header.size, consumer);

      return true;
    }
    else
    {
      return false;
    }
  }

  return pop_from_cache (header, data, consumer);
}

#elif 1
template <class Allocator, uint8_t MaxNoDropConsumers>
template <class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header     &header,
  BufferType &data,
  detail::ConsumerState &consumer)
{
  // if (SPMC_EXPECT_TRUE (!m_cacheEnabled))
  {
    /*
     * If all available data in a consumer has been consumed, request more
     */
    if (consumer.data_range ().empty ())
    {
      auto &backPressure = m_queue->back_pressure ();

      backPressure.consumed (consumer);
      /*
       * Get the size of data available in the queue for a consumer
       */
      size_t read_available = backPressure.read_available (consumer);
      /*
       * Update the consumable range if new data is available
       */
      consumer.data_range ().read_available (read_available);
      /*
       * Return if no new data is available
       */
      if (read_available == 0)
      {
        return false;
      }
    }

    /*
     * Test caching all available consumer data
     */
    if (m_queue->pop_test (header, consumer))
    {
      if (header.type == WARMUP_MESSAGE_TYPE)
      {
        consumer.data_range ().consumed (sizeof (Header));

        return false;
      }

      data.resize (header.size);

      m_queue->pop_test (data.data (), header.size, consumer);

      consumer.data_range ().consumed (sizeof (Header) + header.size);

      return true;
    }
    else
    {
      return false;
    }
  }

  return pop_from_cache (header, data, consumer);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
template <class POD>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  POD &pod,
  detail::ConsumerState &consumer)
{
#pragma message "remove local buffer cache"
  // if (SPMC_EXPECT_TRUE (!m_cacheEnabled))
  {
    /*
     * If all available data in a consumer has been consumed, request more
     */
    if (consumer.data_range ().empty ())
    {
      auto &backPressure = m_queue->back_pressure ();

      backPressure.consumed (consumer);
      /*
       * Get the size of data available in the queue for a consumer
       */
      size_t read_available = backPressure.read_available (consumer);
      /*
       * Update the consumable range if new data is available
       */
      consumer.data_range ().read_available (read_available);
      /*
       * Return if no new data is available
       */
      if (read_available == 0)
      {
        return false;
      }
    }

    /*
     * Test caching all available consumer data
     */
    if (m_queue->pop_test (pod, consumer))
    {
      consumer.data_range ().consumed (sizeof (POD));

      return true;
    }
  }

  return false;
}

#else
template <class Allocator, uint8_t MaxNoDropConsumers>
template <class Header, class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header     &header,
  BufferType &data,
  detail::ConsumerState &consumer)
{
  /*
   * Reserve a data range of data to be read by the consumer
   */
  auto update_data_range = [&] () {
    /*
     * If the local consumable data range is empty, refresh read available
     */
    if (consumer.data_range ().read_available () == 0)
    {
      consumer.data_range ().read_available (m_queue->read_available (consumer));
    }

    if (consumer.data_range ().consumed_all ())
    {
      /*
       * After consuming the available data range update back pressure
       */
      m_queue->back_pressure ().consumed (consumer, consumer.data_range ().consumed ());

      consumer.data_range ().reset ();

      consumer.data_range ().read_available (m_queue->read_available (consumer));
    }
  };

  if (SPMC_EXPECT_TRUE (!m_cacheEnabled))
  {
    while (consumer.data_range ().read_available () == 0)
    {
      update_data_range ();
    }

    /*
    * Test caching all available consumer data
    */
    if (m_queue->pop_test (header, consumer))
    {
      consumer.data_range ().consumed (sizeof (Header));

      if (header.type == WARMUP_MESSAGE_TYPE)
      {
        return false;
      }

      // std::cout << "header.size: " << header.size << std::endl;
      data.resize (header.size);

      m_queue->pop_test (data.data (), header.size, consumer);

      consumer.data_range ().consumed (header.size);

      return true;
    }
    else
    {
      return false;
    }
  }

  return pop_from_cache (header, data, consumer);
}

#endif
template <class Allocator, uint8_t MaxNoDropConsumers>
template <class Header, class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop_from_cache (
  Header     &header,
  BufferType &data,
  detail::ConsumerState &consumer)
{
  if (!m_cacheEnabled)
  {
    return false;
  }
#if TODO // Restart is not currently supported with the current algorithm
  if (m_queue->producer_restarted (m_consumer) && !m_cache.empty ())
  {
    BOOST_LOG_TRIVIAL (info)
      << "Producer restarted. Clear the consumer prefetch cache.";
    m_cache.clear ();
  }
#endif
  if (m_cache.size () <= sizeof (Header))
  {
    /*
     * Move a chunk of data from the shared queue into the local cache.
     *
     * The cache will be increased in size if it is too small.
     */
    m_queue->prefetch_to_cache (m_cache, consumer);
  }

  if (m_cache.pop (header) && header.type != WARMUP_MESSAGE_TYPE)
  {
    if (m_cache.capacity () < header.size)
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

      assert (m_queue->pop (tmp.data (), header.size - data.size (), consumer));

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
      m_queue->prefetch_to_cache (m_cache, consumer);
    }

    return m_cache.pop (data, header.size);
  }

  return false;
}

} // namespace spmc {
