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
    m_consumerIndexes[i] = Cursor::UnInitialised;
  }
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::register_consumer (
  ConsumerState &consumer)
{
  std::lock_guard<Mutex> g (m_mutex);

  BOOST_LOG_TRIVIAL (info) << "Register consumer";

  CHECK_SS (m_maxConsumers < std::numeric_limits<uint8_t>::max (),
            "Too many consumers requested, max: "
              << std::numeric_limits<uint8_t>::max ());
  /*
   * SPMCBackPressure supports a limited number of consumer threads
   */
  CHECK_SS (m_maxConsumers < MaxNoDropConsumers,
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
    if (m_consumerIndexes[i] == Cursor::UnInitialised)
    {
      m_consumerIndexes[i] = m_committed;
      index          = i;
      registered     = true;
      break;
    }
    BOOST_LOG_TRIVIAL (debug)
      << "Consumer index not available: m_consumerIndexes[" << static_cast<size_t> (i)
      << "]=" << cursor_to_string (m_consumerIndexes[i]);
  }
  /*
   * If there are no unused slots, use a new slot if one is available
   */
  if (!registered)
  {
    m_consumerIndexes[m_maxConsumerIndex] = m_committed;

    index = m_maxConsumerIndex;

    ++m_maxConsumerIndex;
  }

  ++m_maxConsumers;
  /*
   * Initialise the consumer cursor to start at the current latest data
   */
  consumer.cursor (m_consumerIndexes[index]);
  /*
   * Set the index used by the producer to exert back pressure
   */
  consumer.index (index);

  BOOST_LOG_TRIVIAL (info) << "Registered consumer index="
                           << std::to_string (index)
                           << " consumer count="
                           << static_cast<size_t> (m_maxConsumers);

  BOOST_LOG_TRIVIAL (debug)
    << "max consumer index=" << static_cast<size_t> (m_maxConsumerIndex)
    << "|cursor=" << cursor_to_string (consumer.cursor ())
    << "|write available=" << write_available (consumer.cursor (), m_claimed);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::unregister_consumer (
  const ConsumerState &consumer)
{
  std::lock_guard<Mutex> g (m_mutex);

  if (is_valid_cursor (consumer.cursor ()))
  {
    m_consumerIndexes[consumer.index ()] = Cursor::UnInitialised;

    --m_maxConsumers;

    BOOST_LOG_TRIVIAL (debug) << "Unregistered consumer (index="
                              << index_to_string (consumer.index ()) << ")";
    BOOST_LOG_TRIVIAL (debug) << "Consumer count: " << std::to_string (m_maxConsumers);
  }
}

template<class Mutex, uint8_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::committed_cursor () const
{
  return m_committed.load (std::memory_order_release);
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
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::write_available ()
{
  uint8_t maxConsumers = m_maxConsumers.load (std::memory_order_relaxed);

  if (BOOST_LIKELY (maxConsumers > 0))
  {
    /*
     * Get the bytes consumed by the slowest consumer.
     */
    uint8_t consumerCount = 0;

    size_t minAvailable = Cursor::UnInitialised;

    for (uint8_t i = m_consumerIndex; consumerCount < MaxNoDropConsumers; ++i)
    {
      /*
       * Rotate the order of the client first served data for improved fairness
       */
      size_t consumerCursor = m_consumerIndexes[MODULUS (i, MaxNoDropConsumers)];

      if (is_valid_cursor (consumerCursor))
      {
        minAvailable = std::min (minAvailable,
                                 write_available (consumerCursor, m_claimed));

        if (minAvailable != Cursor::UnInitialised)
        {
          ++consumerCount;
        }
      }

      if (consumerCount == maxConsumers ||
          m_maxConsumers.load (std::memory_order_relaxed) == 0)
      {
        break;
      }
    }
    /*
     * Update consumer servicing order after each data push
     */
    m_consumerIndex = ((m_consumerIndex + 1)
                          < m_maxConsumers.load (std::memory_order_relaxed))
                    ? m_consumerIndex + 1 : 0;

    return minAvailable;
  }

  return m_maxSize - 1;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::update_consumer_state (
  ConsumerState &consumer)
{
  auto &cursor = m_consumerIndexes[consumer.index ()];

  cursor = advance_cursor (cursor, consumer.data_range ().consumed ());

  consumer.cursor (cursor);
}

} // namespace detail {
} // namespace spmc {
