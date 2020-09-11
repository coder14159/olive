#ifndef IPC_PERFORMANCE_STATS_H
#define IPC_PERFORMANCE_STATS_H

#include "LatencyStats.h"
#include "ThroughputStats.h"

#include <boost/lockfree/spsc_queue.hpp>

#include <thread>

namespace spmc {

class PerformanceStats
{
public:
  struct Dropped
  {
    uint64_t interval = 0;
    uint64_t summary  = 0;
  };

public:
  PerformanceStats ();

  PerformanceStats (const std::string &path);

  ~PerformanceStats ();

  void output_directory (const std::string &path);

  /*
   * Messages dropped
   */
  const PerformanceStats::Dropped &dropped () const;

  /*
   * Update number of bytes sent at a time point
   */
  void update (uint64_t bytes, uint64_t seqNum, TimePoint timestamp);

  /*
   * Update when receiving a new message
   */
  void update (uint64_t header_size, uint64_t payload_size,
               uint64_t seqNum, TimePoint timestamp);

  const ThroughputStats &throughput () const { return m_throughput; }
        ThroughputStats &throughput ()       { return m_throughput; }

  const LatencyStats &latency () const { return m_latency;    }
        LatencyStats &latency ()       { return m_latency;    }

private:

  /*
   * Start the service thread
   */
  void start ();

  /*
   * Stop the service thread
   */
  void stop ();

  void log_interval_stats ();

private:

  Dropped m_dropped;

  ThroughputStats m_throughput;

  LatencyStats m_latency;

  TimePoint m_lastIntervalLog;

  TimePoint m_sampled;

  TimePoint m_startTime;

  const uint64_t QUEUE_SIZE = { 10 };

  boost::lockfree::spsc_queue<Clock::duration> m_queue { QUEUE_SIZE };

  std::thread m_thread;

  uint64_t m_seqNum = 0;

  std::atomic_bool m_stop { false };

};

} // namespace spmc {

#include "PerformanceStats.inl"

#endif
