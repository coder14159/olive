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
{ }

LatencyStats::LatencyStats (const std::string &directory)
: m_summary (directory, "latency-summary.csv")
, m_interval (directory, "latency-interval.csv")
{ }

LatencyStats::~LatencyStats ()
{

}

bool LatencyStats::is_stopped () const
{
  return (m_interval.is_stopped () && m_summary.is_stopped ());
}

} // namespace spmc
