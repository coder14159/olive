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
  /*
   * Check for dropped messages
   */
  if (SPMC_EXPECT_TRUE (m_seqNum > 1))
  {
    auto dropped = seqNum - m_seqNum;

    if (SPMC_EXPECT_FALSE (dropped > 1))
    {
      m_dropped.interval += dropped;
      m_dropped.summary  += dropped;
    }
  }

  m_seqNum = seqNum;

  m_throughput.interval ().next (bytes, seqNum);
  m_throughput.summary ().next (bytes, seqNum);
  /*
   * Sample latency values as requesting a timestamp too often impacts
   * performance
   */
  Nanoseconds durationSinceSampled (timestamp - m_sampled);

  if (durationSinceSampled < 10us)
  {
    return;
  }

  m_sampled = Clock::now ();

  m_queue.push ({ m_sampled - timestamp });

  m_bytes = 0;
}

inline
void PerformanceStats::update (uint64_t header, uint64_t payload,
                               uint64_t seqNum, TimePoint timestamp)
{
  update (header + payload, seqNum, timestamp);
}

} // namespace spmc {
