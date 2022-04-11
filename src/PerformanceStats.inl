#include "Chrono.h"
#include "PerformanceStats.h"
#include "TimeDuration.h"
#include "detail/SharedMemory.h"
#include "detail/Utils.h"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <thread>

using namespace std::chrono_literals;

namespace olive {

inline
void PerformanceStats::update (uint64_t bytes, uint64_t seqNum,
                               TimePoint timestamp)
{
  /*
   * Record all throughput data
   */
  m_intervalBytes += bytes;
  ++m_intervalMessages;

  /*
   * Requesting a timestamp too often impacts performance, therefore latency
   * values are sampled
   */
  if ((timestamp - m_sampled) < 5us)
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

} // namespace olive {
