#include "detail/SharedMemory.h"

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

  ++m_counter;

  auto targetInterval = Seconds (m_counter / m_rate);

  auto intervalStart = Clock::now ();

  auto actualInterval = intervalStart - m_startTime;

  while (actualInterval < targetInterval)
  {
    std::this_thread::sleep_for (targetInterval - actualInterval);

    actualInterval = Clock::now () - m_startTime;
  }

  /*
    * Periodically reset the counters so that the throttle is better able to
    * handle variations in workload.
    */
  if ((intervalStart - m_startTime) > Seconds (1))
  {
    m_startTime = intervalStart;
    m_counter = 0;
  }
}

template<typename Sink>
void Throttle::throttle (Sink &sink)
{
  /*
   * No throttling by default
   */
  if (m_rate == 0)
  {
    return;
  }

  using namespace std::chrono_literals;

  ++m_counter;

  auto targetInterval = Seconds (m_counter / m_rate);

  auto intervalStart = Clock::now ();

  auto actualInterval = intervalStart - m_startTime;

  auto cycleInterval =
    std::min (Nanoseconds {intervalStart - m_startTime}, 10ns);

  while (actualInterval < targetInterval)
  {
    std::this_thread::sleep_for (cycleInterval);

    sink.next_keep_warm ();

    actualInterval = Clock::now () - m_startTime;
  }

  /*
    * Periodically reset the counters so that the throttle is better able to
    * handle variations in workload.
    */
  if ((intervalStart - m_startTime) > Seconds(1))
  {
    m_startTime = Clock::now ();
    m_counter = 0;
  }
}


} // namespace spmc
