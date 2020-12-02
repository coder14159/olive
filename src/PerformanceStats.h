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
  PerformanceStats ();

  PerformanceStats (const std::string &path);

  ~PerformanceStats ();

  void output_directory (const std::string &path);

  /*
   * Update number of bytes received sequence number at a time point
   */
  void update (uint64_t bytes, uint64_t seqNum, TimePoint timestamp);

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

  ThroughputStats m_throughput;

  LatencyStats m_latency;

  TimePoint m_lastIntervalLog;

  TimePoint m_sampled;

  TimePoint m_startTime;
  /*
   * Store sampled latency values
   */
  static const int QUEUE_CAPACITY = 10;
  boost::lockfree::spsc_queue<Clock::duration,
              boost::lockfree
                   ::capacity<sizeof (Clock::duration)*QUEUE_CAPACITY>> m_queue;

  std::thread m_thread;

  uint64_t m_seqNum = 0;

  std::atomic_bool m_stop { false };

};

} // namespace spmc {

#include "PerformanceStats.inl"

#endif
