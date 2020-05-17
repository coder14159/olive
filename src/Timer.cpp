#include "Timer.h"

namespace spmc {

Timer::Timer ()
: m_begin (Time::now ())
, m_end (INVALID_TIME_POINT)
{ }

void Timer::start ()
{
  reset ();

  m_begin = Time::now ();
}

void Timer::stop ()
{
  m_end = Time::now ();
}

Timer& Timer::reset ()
{
  *this = Timer ();

  return *this;
}

bool Timer::is_stopped () const
{
  return (m_end != INVALID_TIME_POINT);
}

bool Timer::is_valid_interval () const
{
  return (m_begin != INVALID_TIME_POINT && m_end != INVALID_TIME_POINT);
}

TimeDuration Timer::elapsed () const
{
  if (is_stopped ())
  {
    return (m_end - m_begin);
  }

  return (Time::now () - m_begin);
}


} // namespace spmc
