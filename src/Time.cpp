#include "Time.h"
#include <iostream>

namespace spmc {

std::string nanoseconds_to_pretty (int64_t nanoseconds)
{
  char buffer [1024];
  buffer[0] = '\0';

  if (nanoseconds > 1e6)
  {
  	double secs = nanoseconds / 1.0e6;
    snprintf (buffer, sizeof (buffer), "%9.0f ms", secs);
  }
  else if (nanoseconds > 1e3)
  {
  	double secs = nanoseconds / 1.0e3;
    snprintf (buffer, sizeof (buffer), "%9.0f us", secs);
  }
  else
  {
    snprintf (buffer, sizeof (buffer), "%9ld ns", nanoseconds);
  }

  return buffer;
}

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds)
{
  return nanoseconds_to_pretty (nanoseconds.count ());
}

int64_t nanoseconds_since_epoch (const TimePoint &time_point)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds> (
            time_point.time_since_epoch ()).count ();
}

TimePoint Time::now ()
{
  return TimePoint (Clock::now ());
}

} // namespace spmc
