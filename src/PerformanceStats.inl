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
  if (seqNum < m_seqNum)
  {
    /*
     * Assume the message producer has restarted if the sequence number is lower
     * than expected.
     */
    m_seqNum = seqNum;

    return;
  }

  /*
   * Check for dropped messages
   */
  if (SPMC_EXPECT_TRUE (m_seqNum > 1))
  {
    auto diff = seqNum - m_seqNum;

    if (SPMC_EXPECT_FALSE (diff > 1))
    {
      m_dropped.interval += diff;
      m_dropped.summary  += diff;
    }
  }

  m_seqNum = seqNum;

  m_throughput.interval ().next (bytes, seqNum);
  m_throughput.summary ().next (bytes, seqNum);

  /*
   * Sample latency values as requesting a timestamp too often impacts
   * performance
   */
  if (SPMC_EXPECT_FALSE ((timestamp - m_sampled) < 10ns))
  {
    return;
  }

  m_sampled = Clock::now ();

  m_queue.push ({ m_sampled - timestamp });
}

inline
void PerformanceStats::update (uint64_t header, uint64_t payload,
                               uint64_t seqNum, TimePoint timestamp)
{
  update (header + payload, seqNum, timestamp);
}

} // namespace spmc {
