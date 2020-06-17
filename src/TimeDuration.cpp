#include "TimeDuration.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <string>

#include <boost/format.hpp>

namespace spmc {

TimeDuration::TimeDuration ()
: m_nanoseconds (0)
{ }

TimeDuration::TimeDuration (Nanoseconds nanoseconds)
: m_nanoseconds (nanoseconds)
{ }

std::string TimeDuration::pretty () const
{
  return nanoseconds_to_pretty (m_nanoseconds);
}

std::string nanoseconds_to_pretty (int64_t nanoseconds)
{
  if (nanoseconds == std::numeric_limits<int64_t>::max ())
  {
    return "0 ns";
  }
  else if (nanoseconds == std::numeric_limits<int64_t>::min ())
  {
    return "0 ns";
  }
  else if (nanoseconds < 1e3)
  {
    return (boost::str (boost::format ("%d ns") % nanoseconds));
  }
  else if (nanoseconds < 1e6)
  {
    double usecs = static_cast<double> (nanoseconds) / 1.0e3;
    return (boost::str (boost::format ("%.0f us") % usecs));
  }
  else if (nanoseconds < 1e9)
  {
    double msecs = static_cast<double> (nanoseconds) / 1.0e6;
    return (boost::str (boost::format ("%.0f ms") % msecs));
  }
  else if (nanoseconds < (1e9*60))
  {
    double secs = static_cast<double> (nanoseconds) / 1.0e9*60;
    return (boost::str (boost::format ("%.0f s") % secs));
  }
  else
  {
    double mins = static_cast<double> (nanoseconds) / (1e9*60);
    return (boost::str (boost::format ("%.0f min") % mins));
  }
}

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds)
{
  return nanoseconds_to_pretty (nanoseconds.count ());
}

} // namespace spmc
