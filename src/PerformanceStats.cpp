#include "Chrono.h"
#include "PerformanceStats.h"
#include "TimeDuration.h"
#include "detail/Utils.h"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <thread>

using namespace std::chrono_literals;

namespace spmc {
namespace {

static const Seconds RESET_DURATION (1);

} // namespace spmc

PerformanceStats::PerformanceStats ()
: m_sampled (Clock::now ())
{
  start ();
}

PerformanceStats::PerformanceStats (const std::string &directory)
: m_throughput (directory)
, m_latency (directory)
, m_sampled (Clock::now ())
{
  start ();
}

void PerformanceStats::start ()
{
  if (m_thread.joinable ())
  {
    BOOST_LOG_TRIVIAL (warning) << "Performance thread already running";

    return;
  }
  /*
   * Service latency information in a separate thread so that persisting latency
   * values are not on the critical path.
   */
  m_thread = std::thread ([this] ()
  {
    Clock::duration latency_duration;

    TimePoint now = Clock::now ();
    TimePoint lastLog = now;

    while (!m_stop)
    {
      if (m_throughput.is_stopped () && m_latency.is_stopped ())
      {
        break;
      }

      if (!m_queue.pop (latency_duration))
      {
        /*
         * Avoid using too much CPU time
         */
        std::this_thread::sleep_for (1us);

        continue;
      }

      now = Clock::now ();

      if ((now - lastLog) > Seconds (1))
      {
        log_stats ();

        m_latency.interval ().write_data ();
        m_latency.interval ().reset ();

        m_throughput.interval ().write_data ();
        m_throughput.interval ().reset ();

        lastLog = now;
      }

      m_latency.interval ().next (latency_duration);
      m_latency.summary ().next (latency_duration);
    }

    m_throughput.summary ().write_data ();
    m_latency.summary ().write_data ();
  });
}


PerformanceStats::~PerformanceStats ()
{
  m_stop = true;

  m_thread.join ();
  /*
   * Output message statistics
   */
  if (m_throughput.summary ().is_running ())
  {
    BOOST_LOG_TRIVIAL(info) <<
      boost::algorithm::trim_left_copy (m_throughput.summary ().to_string ());

    BOOST_LOG_TRIVIAL(info) << m_throughput.summary ().dropped ()
                            << " messages dropped";
  }

  for (auto line : m_latency.summary ().to_strings ())
  {
    BOOST_LOG_TRIVIAL(info) << line;
  }
}

void PerformanceStats::log_stats ()
{
  std::string log;

  if (m_latency.interval ().is_running ())
  {
    log += m_latency.interval ().to_string ();

    m_latency.interval ().reset ();
  }

  if (m_throughput.interval ().is_running ())
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
}

void PerformanceStats::update (uint64_t bytes, uint64_t seqNum,
                               TimePoint timestamp)
{
  /*
   * Check for dropped messages
   */
  m_throughput.interval ().next (bytes, seqNum);
  m_throughput.summary ().next (bytes, seqNum);

  if (SPMC_EXPECT_TRUE (m_seqNum > 0))
  {
    if (SPMC_EXPECT_FALSE ((seqNum - m_seqNum) == 1))
    {
      m_throughput.interval ().dropped (seqNum - m_seqNum);
      m_throughput.summary ().dropped (seqNum - m_seqNum);
    }
  }

  m_seqNum = seqNum;

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

void PerformanceStats::update (uint64_t header, uint64_t payload,
                               uint64_t seqNum, TimePoint timestamp)
{
  update (header + payload, seqNum, timestamp);
}

} // namespace spmc {
