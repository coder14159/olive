#include "PerformanceStats.h"
#include "detail/Utils.h"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

using namespace std::chrono_literals;

namespace spmc {

PerformanceStats::PerformanceStats (TimeDuration warmup)
: m_sampled (Clock::now ())
, m_warmupDuration (warmup)
{
  start ();
}

PerformanceStats::PerformanceStats (const std::string &directory,
                                    TimeDuration warmup)
: m_throughput (directory)
, m_latency (directory)
, m_sampled (Clock::now ())
, m_warmupDuration (warmup)
{
  start ();
}

PerformanceStats::~PerformanceStats ()
{
  stop ();
}

void PerformanceStats::stop ()
{
  m_stop = true;

  if (m_thread.joinable ())
  {
    m_thread.join ();
  }
}

void PerformanceStats::start ()
{
  if (m_thread.joinable ())
  {
    BOOST_LOG_TRIVIAL (info) << "Performance thread already running";

    return;
  }

  m_stop = false;
  /*
   * Service latency information in a separate thread so that persisting latency
   * values are not on the critical path.
   */
  m_thread = std::thread ([this] ()
  {
    Stats stats;

    TimePoint now = Clock::now ();

    TimePoint lastLog = now;
    /*
     * Warmup for a few seconds before starting to take latency values
     */
    bool warmup = true;

    while (!m_stop.load (std::memory_order_relaxed))
    {
      if (m_throughput.is_stopped () && m_latency.is_stopped ())
      {
        m_stop = true;
        break;
      }

      if (!m_queue.pop (stats))
      {
        /*
         * Sample latencies to avoid using too much CPU time
         */
        std::this_thread::sleep_for (1us);

        continue;
      }

      now = Clock::now ();

      auto logDuration = now - lastLog;

      /*
       * Allow a warmup period of a few seconds before starting latency logging
       */
      if (SPMC_EXPECT_FALSE (warmup))
      {
        if (logDuration > m_warmupDuration)
        {
          warmup = false;
          lastLog = now;

          m_throughput.interval ().reset ();
          m_throughput.summary ().reset ();

          BOOST_LOG_TRIVIAL (info) << "Warmup complete, "
                                   << "start logging performance statistics";
        }

        continue;
      }

      m_latency.interval ().next (stats.latency);
      m_latency.summary ().next (stats.latency);

      m_throughput.interval ().next (stats.bytes, stats.messages);
      m_throughput.summary ().next (stats.bytes, stats.messages);

      if (logDuration > Seconds (1))
      {
        log_interval_stats ();

        lastLog = now;
      }
    }

    m_throughput.summary ().write_data ();
    m_latency.summary ().write_data ();
  });
}

void PerformanceStats::print_summary () const
{
  if (m_throughput.summary ().is_running ())
  {
    BOOST_LOG_TRIVIAL (info) <<
      boost::algorithm::trim_left_copy (m_throughput.summary ().to_string ());
  }

  for (auto line : m_latency.summary ().to_strings ())
  {
    BOOST_LOG_TRIVIAL (info) << line;
  }

}

void PerformanceStats::log_interval_stats ()
{
  std::string log;

  if (m_latency.interval ().is_running ())
  {
    log += m_latency.interval ().to_string ();

    m_latency.interval ().write_data ().reset ();
  }

  if (m_throughput.interval ().is_running ())
  {
    if (!log.empty ()) { log += "|"; }

    log += m_throughput.interval ().to_string ();

    m_throughput.interval ().write_data ().reset ();
  }

  if (!log.empty ())
  {
    BOOST_LOG_TRIVIAL (info) << log;
  }
}

} // namespace spmc {
