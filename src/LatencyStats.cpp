#include "Chrono.h"
#include "LatencyStats.h"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

using namespace std::chrono_literals;

namespace fs = boost::filesystem;

namespace spmc {
namespace {

static const int64_t RESET_INTERVAL = -1;

} // namespace spmc

LatencyStats::LatencyStats ()
: m_queue (DEFAULT_QUEUE_SIZE)
{ }

LatencyStats::LatencyStats (const std::string &directory)
: m_queue (DEFAULT_QUEUE_SIZE)
, m_summary (directory, "latency-summary.csv")
, m_interval (directory, "latency-interval.csv")
{ }

LatencyStats::LatencyStats (size_t queueSize, const std::string &directory)
: m_queue (queueSize)
, m_summary (directory, "latency-summary.csv")
, m_interval (directory, "latency-interval.csv")
{ }

LatencyStats::~LatencyStats ()
{
  stop ();
}

void LatencyStats::start ()
{
  BOOST_LOG_TRIVIAL(info) << "Start latency statistics";

  if (m_thread.joinable ())
  {
    BOOST_LOG_TRIVIAL(warning)
      << "Failed to start latency statistics, already running";
    return;
  }

  /*
   * Service latency information in a separate thread so that persisting latency
   * values are not on the critical path.
   */
  m_thread = std::thread ([this] ()
  {
    int64_t latency;

    while (!m_stop)
    {
      if (m_interval.is_stopped () && m_summary.is_stopped ())
      {
        break;
      }

      if (!m_queue.pop (latency))
      {
        /*
         * Avoid using too much CPU time
         */
        std::this_thread::sleep_for (1us);

        continue;
      }

      if (!m_interval.is_stopped ())
      {
        /*
         * Reset interval latency if requested
         */
        if (latency == RESET_INTERVAL)
        {
          m_interval.write_data ();
          m_interval.reset ();

          continue;
        }

        m_interval.latency (latency);
      }


      m_summary.latency (latency);
    }

    m_interval.write_data ();
    m_summary.write_data ();

  });

  BOOST_LOG_TRIVIAL(info) << "End latency statistics";
}

void LatencyStats::stop ()
{
  if (!m_stop)
  {
    m_stop = true;

    if (m_thread.joinable ())
    {
      m_thread.join ();
    }
  }
}

void LatencyStats::next (std::chrono::steady_clock::time_point time)
{
  if (m_interval.is_stopped () && m_summary.is_stopped ())
  {
    return;
  }

  /*
   * Use samples of latency information because invoking Time::now () too often
   * can have an impact on the latencies being measured
   */
  std::chrono::nanoseconds duration_since_sampled (time - m_sampled);

  if (duration_since_sampled < std::chrono::microseconds (5))
  {
    return;
  }

  m_sampled = std::chrono::steady_clock::now ();

  int64_t latency = std::chrono::nanoseconds (m_sampled - time).count ();

  m_queue.push (latency);
}

void LatencyStats::interval_reset ()
{
  m_queue.push (RESET_INTERVAL);
}

} // namespace spmc
