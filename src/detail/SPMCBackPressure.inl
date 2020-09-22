#include "Assert.h"

#include <boost/log/trivial.hpp>

#include <mutex>

namespace spmc {

template<class Mutex, size_t MaxNoDropConsumers>
SPMCBackPressure<Mutex, MaxNoDropConsumers>::SPMCBackPressure ()
: m_maxNoDropConsumers (MaxNoDropConsumers)
{
  std::lock_guard<Mutex> g (m_mutex);
  m_consumed.fill (-1);
}

template<class Mutex, size_t MaxNoDropConsumers>
size_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::register_consumer ()
{
  std::lock_guard<Mutex> g (m_mutex);

  /*
   * SPMCBackPressure supports a limited number of consumer threads
   */
  if (m_consumerCount >= m_maxNoDropConsumers)
  {
    BOOST_LOG_TRIVIAL(warning)
      << "Cannot register another consumer: Maximum number of no-drop"
      << " consumers [" << m_maxNoDropConsumers << "] already registered";

    return Consumer::UnInitialised;
  }

  size_t index = 0;

  /*
   * consumers should intialise one at a time
   */
  bool registered = false;

  /*
   * Re-use spare slots for new comsumers if there are any available
   */
  for (size_t i = 0; i < m_maxConsumerIndex; ++i)
  {
    BOOST_LOG_TRIVIAL(trace)
      << "slot check m_consumed[" << i << "]=" << m_consumed[i];

    if (m_consumed[i] == Consumer::Stopped)
    {
      m_consumed[i] = 0;
      index         = i;
      registered    = true;
      break;
    }
  }

  /*
   * If there are no unused slots use a new slot
   */
  if (!registered)
  {
    m_consumed[m_maxConsumerIndex] = 0;

    index = m_maxConsumerIndex;

    ++m_maxConsumerIndex;
  }

  ++m_consumerCount;

  m_hasNonDropConsumers = true;

  BOOST_LOG_TRIVIAL(trace) << "Register consumer: index=" << index
                           << " m_maxConsumerIndex=" << m_maxConsumerIndex
                           << " m_consumerCount="    << m_consumerCount;

  return index;
}

template<class Mutex, size_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::reset_consumers ()
{
  std::lock_guard<Mutex> g (m_mutex);

  for (auto &consumed : m_consumed)
  {
    /*
     * Reset active consumer indexes
     */
    if (consumed != Consumer::UnInitialised && consumed != Consumer::Stopped)
    {
      consumed = 0;
    }
  }
}

template<class Mutex, size_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::unregister_consumer (size_t index)
{
  BOOST_LOG_TRIVIAL(trace) << "Unregister consumer: index=" << index;

  if (index != Consumer::UnInitialisedIndex)
  {
    consumed (Consumer::Stopped, index);
  }
}

template<class Mutex, size_t MaxNoDropConsumers>
bool SPMCBackPressure<Mutex, MaxNoDropConsumers>::has_non_drop_consumers () const
{
  return m_hasNonDropConsumers;
}

template<class Mutex, size_t MaxNoDropConsumers>
uint64_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::min_consumed ()
{
  size_t   consumers = m_maxConsumerIndex;
  uint64_t min       = std::numeric_limits<uint64_t>::max ();
  /*
   * Get the bytes consumed by the slowest consumer.
   *
   * For a more fair iteration do not always start with same consumer thread
   */
  for (size_t i = m_lastIndex; i < (consumers + m_lastIndex ); ++i)
  {
    uint64_t consumed = m_consumed[i - m_lastIndex];

    min = (consumed < min) ? consumed : min;
  }

  m_lastIndex = ((m_lastIndex + 1) > consumers) ? 0 : m_lastIndex + 1;

  // There are no consumers with "no message drop" configuration
  if (min == Consumer::Stopped)
  {
    m_hasNonDropConsumers = false;
  }

  return min;
}

template<class Mutex, size_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::consumed (uint64_t bytes, size_t index)
{
  m_consumed[index] = bytes;

  if (bytes == Consumer::Stopped)
  {
    std::lock_guard<Mutex> g (m_mutex);
    --m_consumerCount;
  }
}

} // namespace spmc {
