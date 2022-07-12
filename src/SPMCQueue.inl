#include "Assert.h"
#include "detail/GetSize.h"

#include <boost/log/trivial.hpp>

#include <cmath>

namespace olive {

template <class Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_queue (std::make_unique<QueueType> (capacity))
{
  CHECK (m_queue.get () != nullptr,
        "In-process SPMCQueue initialisation failed");

  CHECK (capacity > sizeof (Header),
        "SPMCQueue capacity must be greater than header size");
}

template <class Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  const std::string &memoryName,
  const std::string &queueName,
  size_t capacity)
{
  CHECK (capacity > sizeof (Header),
        "SPMCQueue capacity must be greater than header size");
  /*
   * Create named shared memory block, or open it if it already exists
   */
  size_t memory_size = capacity
                     + SharedMemory::BOOK_KEEPING
                     + sizeof (SPMCQueue<SharedMemory::Allocator>);

  namespace bi = boost::interprocess;

  m_memory = bi::managed_shared_memory (bi::open_or_create,
                             memoryName.c_str (), memory_size);

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
  return m_queue->read_available (consumer);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::write_available () const
{
  return m_queue->back_pressure ().write_available ();
}

template <class Allocator, uint8_t MaxNoDropConsumers>
template <class Data>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (const Data &data)
{
  static_assert (std::is_trivially_copyable<Data>::value,
                "Type must be trivially copyable");

  return m_queue->push (data);
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
  m_queue->register_consumer (consumer);
}

template <class Allocator, uint8_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  detail::ConsumerState &consumer)
{
  m_queue->unregister_consumer (consumer);
}
template <class Allocator, uint8_t MaxNoDropConsumers>
template <class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header     &header,
  BufferType &data,
  detail::ConsumerState &consumer)
{
  /*
   * If all available data in a consumer has been consumed, request more data to
   * be added to the consumer cache
   */
  if (consumer.data_range ().empty ())
  {
    auto &backPressure = m_queue->back_pressure ();

    backPressure.update_consumer_state (consumer);
    /*
     * Get the size of data available in the queue for a consumer
     */
    size_t read_available = backPressure.read_available (consumer);
    /*
     * Update the consumable range if new data is available
     */
    consumer.data_range ().read_available (read_available);
    /*
     * Return false if no new data is available
     */
    if (read_available == 0)
    {
      return false;
    }
  }
  /*
   * Test caching all available consumer data
   */
  if (m_queue->pop (header, consumer))
  {
    if (header.type == WARMUP_MESSAGE_TYPE)
    {
      // Warmup messages do not have a payload and are not used by the consumer
      consumer.data_range ().consumed (sizeof (Header));

      return false;
    }

    data.resize (header.size);

    m_queue->pop (data.data (), header.size, consumer);

    consumer.data_range ().consumed (sizeof (Header) + header.size);

    return true;
  }

  return false;
}

template <class Allocator, uint8_t MaxNoDropConsumers>
template <class POD>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  POD &pod,
  detail::ConsumerState &consumer)
{
  /*
   * If all available data in a consumer has been consumed, request more
   */
  if (consumer.data_range ().empty ())
  {
    auto &backPressure = m_queue->back_pressure ();

    backPressure.update_consumer_state (consumer);
    /*
     * Get the size of data available in the queue for a consumer
     */
    size_t read_available = backPressure.read_available (consumer);
    /*
     * Update the consumer range with current data is availability
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
  if (m_queue->pop (pod, consumer))
  {
    consumer.data_range ().consumed (sizeof (POD));

    return true;
  }

  return false;
}

} // namespace olive {
