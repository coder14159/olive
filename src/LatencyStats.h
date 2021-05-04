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
 * Helper class for interval and summary latency logging
 */
class LatencyStats
{
public:

  LatencyStats ();
  /*
   * Output latency statistis values to file in the named directory
   */
  LatencyStats (const std::string &directory);

  ~LatencyStats ();

  /*
   * Stop interval and summary latency threads
   */
  void stop ();
  /*
   * Return true if both interval and summary logging are stopped
   */
  bool is_stopped () const;

  const Latency& interval () const { return m_interval;  }
  const Latency& summary ()  const { return m_summary;   }

  Latency&       interval () { return m_interval;  }
  Latency&       summary ()  { return m_summary;   }

private:

  Latency m_summary;
  Latency m_interval;

};

} // namespace spmc

#include "LatencyStats.inl"

#endif
