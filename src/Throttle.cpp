#include "Throttle.h"

namespace spmc {

Throttle::Throttle (uint32_t rate)
  : m_rate (rate), m_startTime (Clock::now ())
{ }

void Throttle::throttle ()
{
  /*
   * No throttling by default
   */
  if (m_rate == 0)
  {
    return;
  }

  auto now = Clock::now();

  ++m_counter;

  auto targetInterval = Seconds(m_counter / m_rate);
  auto actualInterval = now - m_startTime;

  while (actualInterval < targetInterval)
  {
    std::this_thread::sleep_for (targetInterval - targetInterval);

    actualInterval = Clock::now () - m_startTime;
  }

  /*
    * Periodically reset the counters so that the throttle is better able to
    * handle variations in workload.
    */
  if ((now - m_startTime) > Seconds(1))
  {
    m_startTime = now;
    m_counter = 0;
  }
}

} // namespace spmc
