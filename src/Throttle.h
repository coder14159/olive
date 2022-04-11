#ifndef OLIVE_THROTTLE_H
#define OLIVE_THROTTLE_H

#include "Chrono.h"

#include <thread>

namespace olive {

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

  /*
   * Call throttle method after each message sent to reduce throughput to the
   * requested rate.
   *
   * The Sink is used to sends dummy messages to keep the fast path warm when
   * the throughput is low.
   */
  template<typename Sink>
  void throttle (Sink &sink);

private:

  // Target throughput rate in messages/second
  uint32_t   m_rate    = 0;
  uint64_t   m_counter = 0;

  TimePoint m_startTime;
};

} // namespace olive

#include "Throttle.inl"

#endif
