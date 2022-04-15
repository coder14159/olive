#ifndef OLIVE_TIME_DURATION_H
#define OLIVE_TIME_DURATION_H

#include "Chrono.h"

#include <ctime>
#include <limits>
#include <string>

#include <boost/operators.hpp>

namespace olive {

/*
 * Helper class for time durations
 */
class TimeDuration : boost::totally_ordered<TimeDuration>
{
public:
  /*
   * Default initialisation is zero time duration
   */
  TimeDuration ();

  /*
   * Initialise TimeDuration using a DurationType eg. Nanoseconds, Microseconds,
   * Milliseconds etc
   */
  template<typename DurationType>
  TimeDuration (const DurationType duration);

  /*
   * Return the current time duration in Nanoseconds
   */
  Nanoseconds nanoseconds () const { return m_nanoseconds; }

  /*
   * Return a printed string of the current TimeDuration
   */
  std::string pretty () const;

  friend bool operator< (const TimeDuration lhs, const TimeDuration rhs)
  {
    return lhs.nanoseconds () < rhs.nanoseconds ();
  }

private:

  Nanoseconds m_nanoseconds;
};

/*
 * Pretty print nanoseconds
 */
std::string nanoseconds_to_pretty (Nanoseconds nanoseconds);

/*
 * Pretty print a the value of std::chrono::duration
 */
template<typename DurationType>
std::string to_pretty (const DurationType duration)
{
  return nanoseconds_to_pretty (
    std::chrono::duration_cast<Nanoseconds> (duration));
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

} // namespace olive

#include "TimeDuration.inl"

#endif // OLIVE_TIME_DURATION_H
