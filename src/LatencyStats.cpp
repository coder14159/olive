#include "LatencyStats.h"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

using namespace std::chrono_literals;

namespace fs = boost::filesystem;

namespace spmc {

LatencyStats::LatencyStats ()
{ }

LatencyStats::LatencyStats (const std::string &directory)
: m_summary (directory, "latency-summary.csv")
, m_interval (directory, "latency-interval.csv")
{ }

LatencyStats::~LatencyStats ()
{ }

} // namespace spmc
