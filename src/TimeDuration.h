#ifndef IPC_TIME_DURATION_H
#define IPC_TIME_DURATION_H

#include "Chrono.h"

#include <ctime>
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

/*
 * Cast TimeDuration to floating point time duration values
 */
inline
double to_seconds (TimeDuration duration)
{
  return static_cast<double> (duration.nanoseconds ().count ()) / 1.0e9;
}

inline
double to_milliseconds (TimeDuration duration)
{
  return static_cast<double> (duration.nanoseconds ().count ()) / 1.0e6;
}

inline
double to_microseconds (TimeDuration duration)
{
  return static_cast<double> (duration.nanoseconds ().count ()) / 1.0e3;
}

inline
double to_nanoseconds (TimeDuration duration)
{
  return static_cast<int64_t> (duration.nanoseconds ().count ());
}

} // namespace spmc

#endif // IPC_TIME_DURATION_H
