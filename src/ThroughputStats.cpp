#include "ThroughputStats.h"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace spmc {

ThroughputStats::ThroughputStats ()
{ }

ThroughputStats::ThroughputStats (const std::string &directory)
: m_interval (directory, "throughput-interval.csv")
, m_summary (directory, "throughput-summary.csv")
{ }

ThroughputStats::~ThroughputStats ()
{ }

bool ThroughputStats::is_stopped () const
{
  return (m_interval.is_stopped () && m_summary.is_stopped ());
}

bool ThroughputStats::is_running () const
{
  return (!m_interval.is_stopped () && !m_summary.is_stopped ());
}

} // namespace spmc
