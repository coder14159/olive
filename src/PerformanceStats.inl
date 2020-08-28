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

inline
PerformanceStats::PerformanceStats ()
: m_sampled (Clock::now ())
{
  start ();
}

inline
PerformanceStats::PerformanceStats (const std::string &directory)
: m_throughput (directory)
, m_latency (directory)
, m_sampled (Clock::now ())
{
  start ();
}

inline
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

      TimeDuration duration (now - lastLog);

      m_latency.interval ().next (latency_duration);
      m_latency.summary ().next (latency_duration);

      if (duration.nanoseconds () > Seconds (1))
      {
        log_interval_stats ();

        m_latency.interval ().write_data ().reset ();

        m_throughput.interval ().write_data ().reset ();

        lastLog = now;
      }
    }

    m_throughput.summary ().write_data ();
    m_latency.summary ().write_data ();
  });
}

inline
PerformanceStats::~PerformanceStats ()
{
  m_stop = true;

  m_thread.join ();
  /*
   * Output message statistics
   */
  if (m_throughput.summary ().is_running ())
  {
    namespace ba = boost::algorithm;
    BOOST_LOG_TRIVIAL(info) << "Throughput: "
        << ba::trim_left_copy (m_throughput.summary ().to_string ());
  }

  BOOST_LOG_TRIVIAL(info) << "Messages dropped: " << m_dropped.summary;

  for (auto line : m_latency.summary ().to_strings ())
  {
    BOOST_LOG_TRIVIAL(info) << line;
  }
}

inline
void PerformanceStats::log_interval_stats ()
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

    m_throughput.interval ().write_data ().reset ();
  }

  if (m_dropped.interval > 0)
  {
    if (!log.empty ()) { log += "|"; }

    log += std::to_string (m_dropped.interval);

    m_dropped.interval = 0;
  }

  if (!log.empty ())
  {
    BOOST_LOG_TRIVIAL (info) << log;
  }
}

inline
const PerformanceStats::Dropped& PerformanceStats::dropped () const
{
  return m_dropped;
}

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
