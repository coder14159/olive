#include "ThroughputStats.h"

namespace spmc {

ThroughputStats::~ThroughputStats ()
{
  m_interval.write_data ();
  m_summary .write_data ();
}


void ThroughputStats::output_directory (const std::string &directory)
{
  m_interval.path (directory, "throughput-interval.csv");
  m_summary .path (directory, "throughput-summary.csv");
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

void ThroughputStats::reset_interval ()
{
  m_interval.write_data ().reset ();
}

} // namespace spmc
