#ifndef IPC_TIME_DURATION_H
#define IPC_TIME_DURATION_H

#include "Time.h"

#include <ctime>
#include <chrono>
#include <limits>
#include <string>

#include <boost/operators.hpp>

namespace spmc {

class TimeDuration : boost::totally_ordered<TimeDuration>
{
public:
  TimeDuration ();
  TimeDuration (Nanoseconds nanoseconds);

  Nanoseconds nanoseconds () const { return m_nanoseconds; }

  std::string pretty () const;

private:

  Nanoseconds m_nanoseconds;
};

inline
bool operator< (TimeDuration lhs, TimeDuration rhs)
{
  return lhs.nanoseconds () < rhs.nanoseconds ();
}

inline
bool operator> (TimeDuration lhs, TimeDuration rhs)
{
  return lhs.nanoseconds () > rhs.nanoseconds ();
}

inline
bool operator== (TimeDuration lhs, TimeDuration rhs)
{
  return lhs.nanoseconds () == rhs.nanoseconds ();
}

inline
double to_seconds_floating_point (TimeDuration duration)
{
  using namespace std::chrono;
  return duration_cast<Seconds> (duration.nanoseconds ()).count ();
}

inline
double to_microseconds_floating_point (TimeDuration duration)
{
  using namespace std::chrono;
  return duration_cast<Microseconds> (duration.nanoseconds ()).count ();
}

} // namespace spmc

#endif // IPC_TIME_DURATION_H

