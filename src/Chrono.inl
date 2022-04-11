#include <boost/format.hpp>

namespace olive {

inline
int64_t nanoseconds_since_epoch (const TimePoint &time_point)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds> (
            time_point.time_since_epoch ()).count ();
}

inline
TimePoint timepoint_from_nanoseconds_since_epoch (int64_t nanoseconds)
{
  return TimePoint (Nanoseconds (nanoseconds));
}

} // namespace olive
