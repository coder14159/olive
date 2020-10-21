#include "TimeDuration.h"
#include "Latency.h"

#include <chrono>
#include <string>

#include <boost/format.hpp>

namespace spmc {

TimeDuration::TimeDuration ()
: m_nanoseconds (Nanoseconds::zero ())
{ }

std::string TimeDuration::pretty () const
{
  return nanoseconds_to_pretty (m_nanoseconds);
}

} // namespace spmc
