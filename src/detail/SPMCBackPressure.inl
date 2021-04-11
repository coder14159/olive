#include "Assert.h"
#include "Utils.h"

#include <boost/log/trivial.hpp>

#include <mutex>

namespace spmc {
namespace detail {

template<class Mutex, uint8_t MaxNoDropConsumers>
SPMCBackPressure<Mutex, MaxNoDropConsumers>::SPMCBackPressure (size_t capacity)
: m_maxSize (capacity + 1)
{
  ASSERT_SS (capacity < (std::numeric_limits<size_t>::max ()),
            "Requested queue capacity too large");

  std::lock_guard<Mutex> g (m_mutex);

  m_consumers.fill (Consumer::UnInitialised);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::register_consumer (
  ConsumerState &consumer)
{
  std::lock_guard<Mutex> g (m_mutex);

  BOOST_LOG_TRIVIAL (trace) << "Register consumer";

  ASSERT_SS (m_consumerCount < std::numeric_limits<uint8_t>::max (),
            "Too many consumers requested, max: "
              << std::numeric_limits<uint8_t>::max ());
  /*
   * SPMCBackPressure supports a limited number of consumer threads
   */
  ASSERT_SS (m_consumerCount < MaxNoDropConsumers,
            "Failed to register a new consumer. Maximum consumer count is "
              << static_cast<size_t> (MaxNoDropConsumers));
  /*
   * Intialisation of consumers is serialised to be one at a time
   */
  bool registered = false;
  uint8_t index = 0;

  /*
   * Re-use spare slots for new comsumers if there are any available
   */
  for (uint8_t i = 0; i < m_maxConsumerIndex; ++i)
  {
    BOOST_LOG_TRIVIAL(trace)
      << "Slot check m_consumers[" << static_cast<size_t> (i)
      << "]=" << m_consumers[i];

    if (m_consumers[i] == Consumer::Stopped)
    {
      m_consumers[i] = m_committed;
      index          = i;
      registered     = true;
      break;
    }
  }
  /*
   * If there are no unused slots use a new slot if one is available
   */
  if (!registered)
  {
    m_consumers[m_maxConsumerIndex] = m_committed;

    index = m_maxConsumerIndex;

    ++m_maxConsumerIndex;
  }

  ++m_consumerCount;

  /*
   * Initialise the consumer cursor to start at the current latest data
   */
  consumer.cursor (m_consumers[index]);
  /*
   * Set the index used by the producer to exert back pressure
   */
  consumer.index (index);

  BOOST_LOG_TRIVIAL(trace)
    << "Register consumer: index=" << static_cast<size_t> (index)
    << " m_maxConsumerIndex=" << static_cast<size_t> (m_maxConsumerIndex)
    << " m_consumerCount="    << static_cast<size_t> (m_consumerCount);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::unregister_consumer (
  const ConsumerState &consumer)
{
  std::lock_guard<Mutex> g (m_mutex);

  BOOST_LOG_TRIVIAL (trace) << "Unregister consumer: index="
                            << static_cast<size_t> (consumer.index ());

  if (consumer.index () != Producer::InvalidIndex)
  {
    m_consumers[consumer.index ()] = Consumer::Stopped;

    --m_consumerCount;
  }
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::committed_cursor () const
{
  return m_committed.load (std::memory_order_relaxed);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::claimed_cursor () const
{
  return m_claimed;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::advance_cursor (
  size_t cursor, size_t advance) const
{
  cursor += advance;

  if (BOOST_LIKELY (cursor < m_maxSize))
  {
    return cursor;
  }

  if (BOOST_LIKELY (cursor > m_maxSize))
  {
    return cursor - m_maxSize;
  }

  return 0;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
bool SPMCBackPressure<Mutex, MaxNoDropConsumers>::acquire_space (size_t size)
{
  size_t space = write_available ();

  if (space >= size)
  {
    m_claimed = advance_cursor (m_claimed, size);

    return true;
  }

  return false;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::release_space ()
{
  m_committed.store (m_claimed, std::memory_order_release);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::read_available (
  const ConsumerState &consumer) const
{
  size_t readerCursor = consumer.cursor ();

  if (BOOST_LIKELY (readerCursor < Consumer::Reserved))
  {
    size_t writerCursor = m_committed.load (std::memory_order_acquire);

    if (BOOST_LIKELY (writerCursor >= readerCursor))
    {
      return writerCursor - readerCursor;
    }

    return writerCursor + m_maxSize - readerCursor;
  }

  return 0;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::write_available (
  size_t readerCursor, size_t writerCursor) const
{
  if (BOOST_UNLIKELY (readerCursor >= Consumer::Reserved))
  {
    return readerCursor;
  }

  size_t available = readerCursor - writerCursor - 1;

  if (writerCursor >= readerCursor)
  {
    available += m_maxSize;
  }

  return available;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::write_available () const
{
  if (BOOST_LIKELY (m_consumerCount > 0))
  {
    /*
     * Get the bytes consumed by the slowest consumer.
     */
    uint8_t consumerCount = 0;

    size_t minAvailable = Consumer::Reserved;

    for (uint8_t i = 0; i < MaxNoDropConsumers; ++i)
    {
      size_t available = write_available (m_consumers[i], m_claimed);

      if (available < Consumer::Reserved)
      {
        minAvailable = std::min (minAvailable, available);

        if (minAvailable < Consumer::Reserved)
        {
          ++consumerCount;
        }
      }

      if (consumerCount == m_consumerCount)
      {
        break;
      }
    }

    return minAvailable;
  }

  return m_maxSize - 1;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::consumed (
  ConsumerState &consumer, size_t size)
{
  auto readerIndex = consumer.index ();

  auto cursor = advance_cursor (m_consumers[readerIndex], size);

  m_consumers[readerIndex] = cursor;

  consumer.cursor (cursor);
}

} // namespace detail {
} // namespace spmc {
