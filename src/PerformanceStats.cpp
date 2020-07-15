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
  /*
   * Output message statistics
   */
  if (!m_throughput.summary ().is_stopped ())
  {
    BOOST_LOG_TRIVIAL(info) << m_throughput.summary ().to_string ();

    BOOST_LOG_TRIVIAL(info) << m_throughput.summary ().dropped ()
                            << " messages dropped";
  }

  if (!m_latency.summary ().is_stopped ())
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

  /*
   * Not critical for the log interval to be perfectly accurate
   */
  auto interval = timestamp - m_last;

  if (interval > Seconds (1))
  {
    std::string log;

    if (!m_latency.interval ().is_stopped ())
    {
      if (!log.empty ()) { log += "|"; }

      log += m_latency.interval ().to_string ();

      m_latency.interval_reset ();
    }

    if (!m_throughput.interval ().is_stopped ())
    {
      if (!log.empty ()) { log += "|"; }

      log += m_throughput.interval ().to_string ();

      if (m_throughput.interval ().dropped () > 0)
      {
        log += "|dropped: "
            + std::to_string (m_throughput.interval ().dropped ());
      }

      m_throughput.interval ().write_data ().reset ();
    }

    if (!log.empty ())
    {
      BOOST_LOG_TRIVIAL (info) << log;
    }

    m_last = timestamp;
  }
}

void PerformanceStats::update (uint64_t header,
                               uint64_t payload,
                               uint64_t seqNum,
                               TimePoint timestamp)
{
  update (header + payload, seqNum, timestamp);
}

} // namespace spmc {
