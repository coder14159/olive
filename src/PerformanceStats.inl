#include "Chrono.h"
#include "PerformanceStats.h"
#include "TimeDuration.h"
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
  if (SPMC_EXPECT_FALSE (seqNum < m_seqNum))
  {
    /*
     * Assume the message producer has restarted if the sequence number is lower
     * than expected.
     */
    m_seqNum = seqNum;

    return;
  }

  m_throughput.interval ().next (bytes, seqNum);
  m_throughput.summary ().next (bytes, seqNum);
  /*
   * Sample latency values as requesting a timestamp too often impacts
   * performance
   */
  if (SPMC_EXPECT_FALSE ((timestamp - m_sampled) < 1us))
  {
    return;
  }

  m_sampled = Clock::now ();

  m_queue.push ({ m_sampled - timestamp });

  m_seqNum = seqNum;
}

} // namespace spmc {
