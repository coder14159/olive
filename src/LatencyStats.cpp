#include "LatencyStats.h"

namespace spmc {

static const int64_t RESET_INTERVAL = -1;

LatencyStats::LatencyStats () : m_queue (1000)
{
  service ();
}

LatencyStats::LatencyStats (size_t queueSize)
: m_queue (queueSize)
{
  service ();
}

LatencyStats::~LatencyStats ()
{
  stop ();
}

void LatencyStats::stop ()
{
  if (!m_stop)
  {
    m_stop = true;

    m_thread.join ();
  }
}

void LatencyStats::service ()
{
  /*
   * Service latency information in a separate thread so that latency
   * calculations are not on the critical path.
   */
  m_thread = std::thread ([this] ()
  {
    int64_t latency;

    while (!m_stop)
    {
      if (!m_queue.pop (latency))
      {
        continue;
      }

      if (m_interval.enabled ())
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
}


void LatencyStats::output_directory (const std::string &directory)
{
  m_interval.path (directory, "latency-interval.csv");
  m_summary .path (directory, "latency-summary.csv");
}

void LatencyStats::next (std::chrono::steady_clock::time_point time)
{
  if (!m_interval.enabled () && !m_summary.enabled ())
  {
    return;
  }

  /*
   * Use samples of latency information because Time::now () as
   * quantiles can be somewhat expensive to compute
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

void LatencyStats::reset_interval ()
{
  m_queue.push (RESET_INTERVAL);
}


} // namespace spmc
