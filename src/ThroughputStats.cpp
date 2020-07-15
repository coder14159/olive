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
{
  write ();
}

void ThroughputStats::write ()
{
  m_interval.write_data ();
  m_summary .write_data ();
}

void ThroughputStats::next (uint64_t bytes, uint64_t seqNum)
{
  m_interval.next (bytes, seqNum);
  m_summary .next (bytes, seqNum);
}

void ThroughputStats::next (uint64_t header, uint64_t payload, uint64_t seqNum)
{
  m_interval.next (header, payload, seqNum);
  m_summary .next (header, payload, seqNum);
}

} // namespace spmc
