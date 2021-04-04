#include "Chrono.h"
#include "PerformanceStats.h"
#include "TimeDuration.h"
#include "detail/SharedMemory.h"
#include "detail/Utils.h"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <thread>

using namespace std::chrono_literals;

namespace spmc {

inline
void PerformanceStats::update (uint64_t bytes, uint64_t seqNum,
                               TimePoint timestamp)
{
#if 0 // TODO // is this required?
  if (SPMC_EXPECT_FALSE (seqNum < m_seqNum))
  {
    /*
     * Assume the message producer has restarted if the sequence number is lower
     * than expected.
     */
    m_seqNum = seqNum;
    m_intervalBytes = 0;
    m_intervalMessages = 0;

    return;
  }
#endif
  m_intervalBytes += bytes;
  ++m_intervalMessages;

  /*
   * Sample latency values as requesting a timestamp too often impacts
   * performance
   */
  if (SPMC_EXPECT_FALSE ((timestamp - m_sampled) < 1us))
  {
    return;
  }

  m_sampled = Clock::now ();

  if (m_queue.push ({{ m_sampled - timestamp },
                       m_intervalBytes,
                       m_intervalMessages }))
  {
    m_intervalBytes = 0;
    m_intervalMessages = 0;
  }

  m_seqNum = seqNum;
}

} // namespace spmc {
