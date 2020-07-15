#ifndef IPC_LATENCY_STATS_H
#define IPC_LATENCY_STATS_H

#include "Latency.h"

#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace spmc {

/*
 * Class for logging of latency values
 */
class LatencyStats
{
public:

  LatencyStats ();

  /*
   * Output latency statistis values to file in the named directory
   */
  LatencyStats (const std::string &directory);

  /*
   * Set the data queue size and output latency statistis values to file in the
   * named directory
   */
  LatencyStats (size_t queueSize, const std::string &directory = "");

  ~LatencyStats ();

  void next (std::chrono::steady_clock::time_point time);

  /*
   * Thread safe method to reset the interval, while data is being pushed into
   * the latency stats objects
   */
  void interval_reset ();

  const Latency& interval () const { return m_interval;  }
  const Latency& summary ()  const { return m_summary;   }

  Latency&       interval () { return m_interval;  }
  Latency&       summary ()  { return m_summary;   }

private:

  void start ();

  void stop ();

private:

  const uint64_t DEFAULT_QUEUE_SIZE = 10;

  boost::lockfree::spsc_queue<int64_t> m_queue { DEFAULT_QUEUE_SIZE };

  /*
   * The steady_clock is monotonic (never moves backwards)
   */
  std::chrono
     ::steady_clock
     ::time_point m_sampled = std::chrono::steady_clock::now ();

  Latency m_summary;
  Latency m_interval;

  std::atomic<bool> m_stop    = { false };

  std::thread m_thread;

};

} // namespace spmc

#endif
