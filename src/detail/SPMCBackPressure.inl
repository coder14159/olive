#include "Assert.h"
#include "Utils.h"

#include <boost/log/trivial.hpp>

#include <mutex>

namespace spmc {

template<class Mutex, uint8_t MaxNoDropConsumers>
SPMCBackPressure<Mutex, MaxNoDropConsumers>::SPMCBackPressure ()
: m_maxNoDropConsumers (MaxNoDropConsumers)
{
  std::lock_guard<Mutex> g (m_mutex);
  m_consumed.fill (-1);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
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

  uint8_t index = 0;

  /*
   * Intialisation of consumers is serialised to be one at a time
   */
  bool registered = false;

  /*
   * Re-use spare slots for new comsumers if there are any available
   */
  for (uint8_t i = 0; i < m_maxConsumerIndex; ++i)
  {
    BOOST_LOG_TRIVIAL(trace)
      << "Slot check m_consumed[" << i << "]=" << m_consumed[i];

    if (m_consumed[i] == Consumer::Stopped)
    {
      m_consumed[i] = 0;
      index         = i;
      registered    = true;
      break;
    }
  }

  /*
   * If there are no unused slots available use a new slot
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

template<class Mutex, uint8_t MaxNoDropConsumers>
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

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::unregister_consumer (size_t index)
{
  BOOST_LOG_TRIVIAL(trace) << "Unregister consumer: index=" << index;

  if (index != Consumer::UnInitialisedIndex)
  {
    consumed (Consumer::Stopped, index);
  }
}

template<class Mutex, uint8_t MaxNoDropConsumers>
bool SPMCBackPressure<Mutex, MaxNoDropConsumers>::has_non_drop_consumers () const
{
  return m_hasNonDropConsumers.load (std::memory_order_relaxed);
}

template<class Mutex, uint8_t MaxNoDropConsumers>
uint64_t SPMCBackPressure<Mutex, MaxNoDropConsumers>::min_consumed ()
{
  uint64_t min = m_consumed[0];
  /*
   * Get the bytes consumed by the slowest consumer.
   */
  for (uint8_t i = 1; i < MaxNoDropConsumers; ++i)
  {
    uint64_t consumed = m_consumed[i];

    min = (consumed < min) ? consumed : min;
  }

  // There are no consumers with "no message drop" configuration
  if (SPMC_EXPECT_FALSE (min == Consumer::Stopped))
  {
    m_hasNonDropConsumers = false;
  }

  return min;
}

template<class Mutex, uint8_t MaxNoDropConsumers>
void SPMCBackPressure<Mutex, MaxNoDropConsumers>::consumed (uint64_t bytes, size_t index)
{
  m_consumed[index] = bytes;

  if (SPMC_EXPECT_FALSE (bytes == Consumer::Stopped))
  {
    std::lock_guard<Mutex> g (m_mutex);

    --m_consumerCount;
  }
}

} // namespace spmc {
