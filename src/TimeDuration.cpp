#include "TimeDuration.h"
#include "Latency.h"

#include <chrono>
#include <string>

#include <boost/format.hpp>

namespace spmc {

std::string nanoseconds_to_pretty (Nanoseconds::rep count)
{
  if (count == Nanoseconds::max ().count ())
  {
    return "-";
  }
  else if (count == Nanoseconds::min ().count ())
  {
    return "-";
  }
  else if (count < 1e3)
  {
    return (boost::str (boost::format ("%3d ns") % count));
  }
  else if (count < 1e6)
  {
    double usecs = static_cast<double> (count) / 1.0e3;
    return (boost::str (boost::format ("%3.0f us") % usecs));
  }
  else if (count < 1e9)
  {
    double msecs = static_cast<double> (count) / 1.0e6;
    return (boost::str (boost::format ("%3.0f ms") % msecs));
  }
  else if (count < (1e9*60))
  {
    double secs = static_cast<double> (count) / 1.0e9;
    return (boost::str (boost::format ("%3.0f s") % secs));
  }
  else
  {
    double mins = static_cast<double> (count) / (1e9*60);
    return (boost::str (boost::format ("%3.0f min") % mins));
  }
}

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds)
{
  return nanoseconds_to_pretty (nanoseconds.count ());
}

TimeDuration::TimeDuration ()
: m_nanoseconds (Nanoseconds::zero ())
{ }

std::string TimeDuration::pretty () const
{
  return nanoseconds_to_pretty (m_nanoseconds);
}

} // namespace spmc
