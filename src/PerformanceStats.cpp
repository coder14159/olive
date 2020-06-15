#include "PerformanceStats.h"
#include "LatencyStats.h"
#include "Throughput.h"
#include "Time.h"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <chrono>

namespace fs = boost::filesystem;

namespace spmc {

PerformanceStats::PerformanceStats () : m_last (Clock::now ())
{ }

PerformanceStats::PerformanceStats (const std::string &path)
: m_last (Clock::now ())
{
  output_directory (path);
}

PerformanceStats::~PerformanceStats ()
{
  m_latency.stop ();

  /*
   * Output message statistics
   */
  if (m_latency.summary ().enabled ())
  {
    BOOST_LOG_TRIVIAL(info) << m_throughput.summary ().to_string ();

    for (auto line : m_latency.summary ().to_strings ())
    {
      BOOST_LOG_TRIVIAL(info) << line;
    }
  }
}

void PerformanceStats::output_directory (const std::string &path)
{
  if (!path.empty ())
  {
    fs::create_directories (fs::path (path));

    m_throughput.output_directory (path);
    m_latency.output_directory (path);
  }
}

void PerformanceStats::update (uint64_t bytes, uint64_t seqNum,
                               TimePoint timestamp)
{
  m_latency.next (timestamp);

  m_throughput.next (bytes, seqNum);

  // latency calculation is expensive so disable if not required
  if (m_latency.interval ().enabled ())
  {
    // use the message timestamp it to avoid a system call to the system clock
    auto interval = timestamp - m_last;

    if (interval > Seconds (1))
    {
      BOOST_LOG_TRIVIAL(info) << m_throughput.interval ().to_string ()
            << " min " << nanoseconds_to_pretty (m_latency.interval ().min ())
            << " max " << nanoseconds_to_pretty (m_latency.interval ().max ());

      m_throughput.reset_interval ();
      m_latency   .reset_interval ();

      m_last = Clock::now ();
    }
  }
}

void PerformanceStats::update (uint64_t header,
                               uint64_t payload,
                               uint64_t seqNum,
                               TimePoint timestamp)
{
  m_latency.next (timestamp);

  m_throughput.next (header, payload, seqNum);

  // use the message timestamp it to avoid a system call to the system clock
  auto interval = timestamp - m_last;

  if (interval > Seconds (1) &&
        (m_latency.interval ().enabled () ||
        (m_throughput.interval ().enabled ())))
  {
    std::string log;

    if (m_latency.interval ().enabled ())
    {
      log += " min " + nanoseconds_to_pretty (m_latency.interval ().min ())
          +  " max " + nanoseconds_to_pretty (m_latency.interval ().max ());

      m_latency   .reset_interval ();
    }

    if (m_throughput.interval ().enabled ())
    {
      log += " " + m_throughput.interval ().to_string();

      m_throughput.reset_interval ();
    }

    if (!log.empty ())
    {
      BOOST_LOG_TRIVIAL(info) << log;
    }

    m_last = Clock::now ();
  }

}

} // namespace spmc {
