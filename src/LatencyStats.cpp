#include "LatencyStats.h"

#include <boost/log/trivial.hpp>

#include <filesystem>

using namespace std::chrono_literals;

namespace fs = std::filesystem;

namespace olive {

LatencyStats::LatencyStats ()
{ }

LatencyStats::LatencyStats (const std::string &directory)
: m_summary (directory, "latency-summary.csv")
, m_interval (directory, "latency-interval.csv")
{ }

LatencyStats::~LatencyStats ()
{
  stop ();
}

void LatencyStats::stop ()
{
  m_summary.stop ();
  m_interval.stop ();
}

} // namespace olive
