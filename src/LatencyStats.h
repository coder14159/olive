#ifndef IPC_LATENCY_STATS_H
#define IPC_LATENCY_STATS_H

#include "Latency.h"

#include <boost/lockfree/spsc_queue.hpp>
// TODO is the current spsc queue patched?
// #include "patch/lockfree/spsc_queue.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace spmc {

/*
 * Helper class for latency interval and summary logging
 */
class LatencyStats
{
public:

  LatencyStats ();
  LatencyStats (size_t queueSize);

  ~LatencyStats ();

  void stop ();

  void next (std::chrono::steady_clock::time_point time);

  /*
   * Thread safe method to reset the interval, while data is being pushed into
   * the latency stats objects
   */
  void reset_interval ();

  // set directory to output latency files
  void output_directory (const std::string &directory);

  const Latency& interval () const { return m_interval;  }
  const Latency& summary ()  const { return m_summary;   }

  Latency&       interval () { return m_interval;  }
  Latency&       summary ()  { return m_summary;   }

private:

  void service ();

  boost::lockfree::spsc_queue<int64_t> m_queue;

  /*
   * The steady_clock is monotonic (never moves backwards)
   */
  std::chrono
     ::steady_clock
     ::time_point m_sampled = std::chrono::steady_clock::now ();

  Latency m_summary;
  Latency m_interval;

  std::atomic<bool> m_stop  = { false };

  std::thread m_thread;

};

} // namespace spmc

#endif
