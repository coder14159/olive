#include "PerformanceStats.h"
#include "TimeDuration.h"

#include <boost/log/trivial.hpp>

#include <chrono>

namespace spmc {

PerformanceStats::PerformanceStats ()
: m_last (Clock::now ())
{ }

PerformanceStats::PerformanceStats (const std::string &directory)
: m_throughput (directory)
, m_latency (directory)
, m_last (Clock::now ())
{ }


PerformanceStats::~PerformanceStats ()
{
  m_latency.stop ();

  /*
   * Output message statistics
   */
  // if (m_throughput.summary ().enabled ())
  {
    for (auto line : m_throughput.summary ().to_strings ())
    {
      BOOST_LOG_TRIVIAL(info) << line;
    }
  }

  // if (m_latency.summary ().enabled ())
  {
    for (auto line : m_latency.summary ().to_strings ())
    {
        BOOST_LOG_TRIVIAL(info) << line;
    }
  }
}

void PerformanceStats::update (uint64_t bytes, uint64_t seqNum,
                               TimePoint timestamp)
{
  m_latency.next (timestamp);

  m_throughput.next (bytes, seqNum);

  return;
#if 0
  /*
   * Not critical for the log interval to be perfectly accurate
   */
  auto interval = timestamp - m_last;

  if (interval > Seconds (1))
  {
    std::string log;

    if (!m_latency.interval ().is_stopped ())
    {
      log += " min=" + nanoseconds_to_pretty (m_latency.interval ().min ())
          +  " max=" + nanoseconds_to_pretty (m_latency.interval ().max ());

      m_latency.interval_reset ();
    }

    if (!m_throughput.interval ().is_stopped ())
    {
      log += " " + m_throughput.interval ().to_string ();

      m_throughput.interval ().write_data ().reset ();
    }

    if (!log.empty ())
    {
      BOOST_LOG_TRIVIAL (info) << log;
    }

    m_last = timestamp;
  }
#endif
}

void PerformanceStats::update (uint64_t header,
                               uint64_t payload,
                               uint64_t seqNum,
                               TimePoint timestamp)
{
  update (header + payload, seqNum, timestamp);
}

} // namespace spmc {
