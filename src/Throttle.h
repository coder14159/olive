#ifndef IPC_THROTTLE_H
#define IPC_THROTTLE_H

#include "Chrono.h"

#include <thread>

namespace spmc {

class Throttle
{
public:
  /*
   * Initialise with a target throughput rate in messages/second
   *
   * Set a value of zero for the maximum rate
   */
  Throttle (uint32_t rate);

  /*
   * Call throttle method after each message sent to reduce throughput to the
   * requested rate
   */
  void throttle ();

private:

  // Target throughput rate in messages/second
  uint32_t   m_rate    = 0;
  uint64_t   m_counter = 0;

  TimePoint m_startTime;
};

} // namespace spmc

#endif
