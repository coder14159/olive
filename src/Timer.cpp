#include "Timer.h"

namespace olive {

const TimePoint INVALID_TIME_POINT (Nanoseconds (0));

Timer::Timer ()
: m_begin (Clock::now ())
, m_end (INVALID_TIME_POINT)
{ }

void Timer::start ()
{
  reset ();

  m_begin = Clock::now ();
}

void Timer::stop ()
{
  m_end = Clock::now ();
}

Timer& Timer::reset ()
{
  *this = Timer ();

  return *this;
}

bool Timer::is_stopped () const
{
  return m_end != INVALID_TIME_POINT;
}

TimeDuration Timer::elapsed () const
{
  if (is_stopped ())
  {
    return (m_end - m_begin);
  }

  return (Clock::now () - m_begin);
}


} // namespace olive
