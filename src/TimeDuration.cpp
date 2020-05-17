#include "TimeDuration.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <string>

namespace spmc {

TimeDuration::TimeDuration ()
: m_nanoseconds (0)
{ }

TimeDuration::TimeDuration (Nanoseconds nanoseconds)
: m_nanoseconds (nanoseconds)
{ }

std::string TimeDuration::pretty () const
{
  /*
   * TODO: safer string formatting method!
   */
  using namespace std::chrono;

  char buffer [1024];

  buffer[0] = '\0';

  auto ns_count = m_nanoseconds.count ();

  if (ns_count < 1e3)
  {
    snprintf (buffer, sizeof (buffer), "%ld ns\n", ns_count);
  }
  else if (ns_count < 1e6)
  {
    double usecs = static_cast<double> (ns_count) / (1.0e3);
    snprintf (buffer, sizeof (buffer), "%.3f us\n", usecs);
  }
  else if (ns_count < 1.0e9)
  {
    double msecs = static_cast<double> (ns_count) / (1.0e6);
    snprintf (buffer, sizeof (buffer), "%.3f ms\n", msecs);
  }
  else
  {
    double secs = static_cast<double> (ns_count) / (1.0e9);
    snprintf (buffer, sizeof (buffer), "%.3f s\n", secs);
  }

  return buffer;
}

} // namespace spmc
