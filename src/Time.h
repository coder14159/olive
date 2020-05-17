#ifndef IPC_TIME_H
#define IPC_TIME_H

#include <chrono>
#include <string>

namespace spmc {

using Clock        = std::chrono::steady_clock;
using TimePoint    = std::chrono::time_point<Clock>;
using Seconds      = std::chrono::seconds;
using Nanoseconds  = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

/*
 * Return a human readable string describing duration
 */
std::string nanoseconds_to_pretty (int64_t nanoseconds);

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds);

int64_t nanoseconds_since_epoch (const TimePoint &time_point);

/*
 * Careful with this function.
 *
 * TimePoint must be initialised with min value for is_valid () to work
 */
static const TimePoint INVALID_TIME_POINT = TimePoint::min ();

inline
bool is_valid (TimePoint time_point)
{
  return (time_point != TimePoint::min ());
}

class Time
{
public:

  static TimePoint now ();

};

} // namespace spmc

#endif // IPC_TIME_H

