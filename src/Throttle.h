#ifndef IPC_THROTTLE_H
#define IPC_THROTTLE_H

#include "Time.h"

#include <thread>

namespace spmc {

class Throttle
{
public:
  Throttle (uint32_t rate)
  : m_rate (rate), m_startTime (Time::now ()) { }

  void throttle ()
  {
    if (m_rate == 0)
    {
      return;
    }

    auto now = Time::now ();

    ++m_counter;
    auto targetInterval = Seconds (m_counter/m_rate);
    auto actualInterval = now - m_startTime;

    while (actualInterval < targetInterval)
    {
      std::this_thread::sleep_for (targetInterval - targetInterval);

      actualInterval = Time::now () - m_startTime;
    }

    /*
     * Periodically reset the counters so that the throttle is better able to cope
     * with variations in the machines workload.
     */
    if ((now - m_startTime) > Seconds (1))
    {
      m_startTime = now;
      m_counter   = 0;
    }
  }

private:

  // Target throughput rate in messages/second
  uint32_t   m_rate    = 0;
  uint64_t   m_counter = 0;

  TimePoint m_startTime;
};

} // namespace spmc

#endif
