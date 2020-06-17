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

/*
 * Return a human readable string describing duration
 */
std::string nanoseconds_to_pretty (int64_t nanoseconds);

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds);


/*
 * Cast TimeDuration to a floating point value
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

} // namespace spmc

#endif // IPC_TIME_DURATION_H

