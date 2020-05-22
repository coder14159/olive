#ifndef IPC_PERFORMANCE_STATS_H
#define IPC_PERFORMANCE_STATS_H

#include "LatencyStats.h"
#include "ThroughputStats.h"
#include "Time.h"


namespace spmc {

class PerformanceStats
{
public:
  PerformanceStats ();

  PerformanceStats (const std::string &path);

  ~PerformanceStats ();

  void output_directory (const std::string &path);

  /*
   * Update number of bytes sent at a time point
   */
  void update (uint64_t bytes, uint64_t seqNum, TimePoint timestamp);

  /*
   * Update header size and payload size sent at a time point
   */
  void update (uint64_t header, uint64_t payload, uint64_t seqNum,
               TimePoint timestamp);

  const ThroughputStats &throughput () const { return m_throughput; }
        ThroughputStats &throughput ()       { return m_throughput; }

  const LatencyStats &latency () const { return m_latency;    }
        LatencyStats &latency ()       { return m_latency;    }

private:
  ThroughputStats m_throughput;

  LatencyStats    m_latency;

  size_t m_messageSize;

  TimePoint m_last;
};

} // namespace spmc {

#endif
