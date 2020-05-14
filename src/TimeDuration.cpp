#include "TimeDuration.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <string>

namespace spmc {

using Clock        = std::chrono::steady_clock;
using TimePoint    = std::chrono::time_point<Clock>;

using Seconds      = std::chrono::seconds;
using Nanoseconds  = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

TimeDuration::TimeDuration ()
: m_nanoseconds (0)
{ }

TimeDuration::TimeDuration (Nanoseconds nanoseconds)
: m_nanoseconds (nanoseconds)
{ }

TimeDuration TimeDuration::minimum ()
{
  return std::numeric_limits<int64_t>::min ();
}

TimeDuration TimeDuration::maximum ()
{
  return std::numeric_limits<int64_t>::max ();
}

std::string TimeDuration::pretty () const
{
  char buffer [1024];

  buffer[0] = '\0';

  std::cout << "nanoseconds: " << m_nanoseconds << std::endl;

  if (m_nanoseconds < 1e3)
  {
    snprintf (buffer, sizeof (buffer), "%ld ns\n", m_nanoseconds);
  }
  else if (m_nanoseconds < 1e6)
  {
    double usecs = static_cast<double> (m_nanoseconds) / (1.0e3);
    snprintf (buffer, sizeof (buffer), "%.3f us\n", usecs);
  }
  else if (m_nanoseconds < 1.0e9)
  {
    double msecs = static_cast<double> (m_nanoseconds) / (1.0e6);
    snprintf (buffer, sizeof (buffer), "%.3f ms\n", msecs);
  }
  else
  {
    double secs = static_cast<double> (m_nanoseconds) / (1.0e9);
    snprintf (buffer, sizeof (buffer), "%.3f s\n", secs);
  }

  return buffer;
}

/*
 * Return a human readable string describing duration
 */
// std::string nanoseconds_to_string (int64_t nanoseconds);

} // namespace spmc
