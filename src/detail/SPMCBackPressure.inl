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
  CHECK_SS (capacity < (std::numeric_limits<size_t>::max ()),
            "Requested queue capacity too large");

  std::lock_guard<Mutex> g (m_mutex);

  for (size_t i = 0; i < MaxNoDropConsumers; ++i)
  {
    m_consumers[i] = Cursor::UnInitialised;
  }
}

#pragma message "Delete method - not needed"
template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::register_producer ()
{
  std::lock_guard<Mutex> g (m_mutex);

  m_claimed = m_committed;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::register_consumer (
  ConsumerState &consumer)
{
  std::lock_guard<Mutex> g (m_mutex);

  BOOST_LOG_TRIVIAL (info) << "Register consumer";

  CHECK_SS (m_consumerCount < std::numeric_limits<uint8_t>::max (),
            "Too many consumers requested, max: "
              << std::numeric_limits<uint8_t>::max ());
  /*
   * SPMCBackPressure supports a limited number of consumer threads
   */
  CHECK_SS (m_consumerCount < MaxNoDropConsumers,
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
    if (m_consumers[i] == Cursor::UnInitialised)
    {
      m_consumers[i] = m_committed;
      index          = i;
      registered     = true;
      break;
    }
    BOOST_LOG_TRIVIAL (debug)
      << "Consumer index not available: m_consumers[" << static_cast<size_t> (i)
      << "]=" << cursor_to_string (m_consumers[i]);
  }
  /*
   * If there are no unused slots, use a new slot if one is available
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

  BOOST_LOG_TRIVIAL (info) << "Registered consumer index="
                           << std::to_string (index);

  BOOST_LOG_TRIVIAL (debug)
    << "consumer count=" << static_cast<size_t> (m_consumerCount)
    << " max consumer index=" << static_cast<size_t> (m_maxConsumerIndex)
    << " cursor=" << cursor_to_string (consumer.cursor ())
    << " write available=" << write_available (consumer.cursor (), m_claimed);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::unregister_consumer (
  const ConsumerState &consumer)
{
  std::lock_guard<Mutex> g (m_mutex);

  if (is_valid_cursor (consumer.cursor ()))
  {
    m_consumers[consumer.index ()] = Cursor::UnInitialised;

    BOOST_LOG_TRIVIAL (debug) << "Unregistered consumer (index="
                             << index_to_string (consumer.index ()) << ")";

    --m_consumerCount;
  }
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::committed_cursor () const
{
  return m_committed.load (std::memory_order_relaxed);
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
  size_t writerCursor = m_committed.load (std::memory_order_acquire);

  if (BOOST_LIKELY (is_valid_cursor (writerCursor)))
  {
    if (writerCursor >= readerCursor)
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

    size_t minAvailable = Cursor::UnInitialised;

    for (uint8_t index = 0; index < MaxNoDropConsumers; ++index)
    // for (uint8_t index = m_consumerIndex;
    //      consumerCount < m_consumerCount; ++index)
    //      index = ((index + 1) < MaxNoDropConsumers) ? index + 1 : 0)
    {
      size_t consumerCursor = m_consumers[index];

      if (is_valid_cursor (consumerCursor))
      {
        size_t available = write_available (consumerCursor, m_claimed);

        minAvailable = std::min (minAvailable, available);

        if (minAvailable != Cursor::UnInitialised)
        {
          ++consumerCount;
        }

        if (consumerCount == m_consumerCount)
        {
          break;
        }
      }
      // index = ((index + 1) < MaxNoDropConsumers) ? index + 1 : 0;
    }

    return minAvailable;
  }

  return m_maxSize - 1;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::consumed (
  ConsumerState &consumer)
{
  auto &cursor = m_consumers[consumer.index ()];

  cursor = advance_cursor (cursor, consumer.data_range ().consumed ());

  consumer.cursor (cursor);
}

} // namespace detail {
} // namespace spmc {
