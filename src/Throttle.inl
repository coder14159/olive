#include "detail/SharedMemory.h"

namespace olive {

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

  using namespace std::chrono_literals;

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
  if ((intervalStart - m_startTime) > 1s)
  {
    m_startTime = intervalStart;
    m_counter = 0;
  }
}

template<typename Sink>
void Throttle::throttle (Sink &sink)
{
  /*
   * No throttling if requesting maximum throughput
   */
  if (m_rate == 0)
  {
    return;
  }

  using namespace std::chrono_literals;

  ++m_counter;

  auto targetInterval = Seconds (m_counter / m_rate);

  /*
   * On the test machine: Intel(R) Core(TM) i5-3450 CPU @ 3.10GHz
   * the duration of a Clock::now () call is ~20 nanoseconds
   */
  auto intervalStart = Clock::now ();

  auto currentInterval = intervalStart - m_startTime;

  auto keepWarmInterval = std::min (currentInterval, 1000ns);

  auto iterationInterval = targetInterval - 500ns;

  while (currentInterval < iterationInterval)
  {
    std::this_thread::sleep_for (keepWarmInterval - 200ns);

    currentInterval = Clock::now () - m_startTime;

    sink.next_keep_warm ();
  }
  /*
   * Periodically reset the counters so that the throttle is better able to
   * handle variations in workload.
   */
  if (currentInterval > 500ms)
  {
    m_startTime = Clock::now ();
    m_counter = 0;
  }
}


} // namespace olive
