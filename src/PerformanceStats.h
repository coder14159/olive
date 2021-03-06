#ifndef OLIVE_PERFORMANCE_STATS_H
#define OLIVE_PERFORMANCE_STATS_H

#include "LatencyStats.h"
#include "ThroughputStats.h"
#include "TimeDuration.h"

#include <boost/lockfree/spsc_queue.hpp>

#include <thread>

namespace olive {

/*
 * PerformanceStats is used to generate throughput and sampled
 * latency performance statistics.
 *
 * Data is output to std::cout and optionally to files
 */
class PerformanceStats
{
public:
  /*
   * Log performance statistics to screen
   */
  PerformanceStats (TimeDuration warmup = Nanoseconds (0));

  /*
   * Log performance statistics to screen and file
   */
  PerformanceStats (const std::string &path,
                    TimeDuration warmup = Nanoseconds (0));

  ~PerformanceStats ();
  /*
   * Start the service thread
   */
  void start ();
  /*
   * Stop the service thread
   */
  void stop ();

  void output_directory (const std::string &path);

  /*
   * Update number of bytes received sequence number at a time point
   */
  void update (uint64_t bytes, uint64_t seqNum, TimePoint timestamp);

  const ThroughputStats &throughput () const { return m_throughput; }
        ThroughputStats &throughput ()       { return m_throughput; }

  const LatencyStats &latency () const { return m_latency;    }
        LatencyStats &latency ()       { return m_latency;    }

  /*
   * Print summary statistics to screen
   */
  void print_summary () const;

private:

  void log_interval_stats ();

private:

  /*
   * Store sampled latency values
   */
  static const size_t QUEUE_CAPACITY = 10;

  struct Stats
  {
    Clock::duration latency;
    uint64_t bytes = { 0 };
    uint64_t messages = { 0 };
  };

  uint64_t m_intervalBytes = { 0 };
  uint64_t m_intervalMessages = { 0 };
  uint64_t m_seqNum = { 0 };

  std::atomic<bool> m_stop { false };

  ThroughputStats m_throughput;

  LatencyStats m_latency;

  boost::lockfree::spsc_queue<Stats,
              boost::lockfree
                   ::capacity<sizeof (Stats)*QUEUE_CAPACITY>> m_queue;

  TimePoint m_lastIntervalLog;

  TimePoint m_sampled;

  TimePoint m_startTime;

  TimeDuration m_warmupDuration;

  std::thread m_thread;

};

} // namespace olive {

#include "PerformanceStats.inl"

#endif
