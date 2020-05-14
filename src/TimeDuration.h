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

  static TimeDuration minimum ();
  static TimeDuration maximum ();

  int64_t nanoseconds () const { return m_nanoseconds.count (); }

  std::string pretty () const;

  // std::string pretty (int64_t nanoseconds) const;

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

} // namespace spmc

#endif // IPC_TIME_DURATION_H

