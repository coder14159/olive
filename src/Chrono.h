#ifndef IPC_CHRONO_H
#define IPC_CHRONO_H

#include <chrono>
#include <string>

namespace spmc {

/*
 * Helper aliases
 */
using Clock        = std::chrono::steady_clock;
using TimePoint    = std::chrono::time_point<Clock>;
using Nanoseconds  = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;
using Seconds      = std::chrono::seconds;

/*
 * Functions returning a human readable duration string
 */
std::string nanoseconds_to_pretty (int64_t nanoseconds);

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds);

/*
 * Helper conversion functions
 */
int64_t nanoseconds_since_epoch (const TimePoint &time_point);

TimePoint timepoint_from_nanoseconds_since_epoch (int64_t nanoseconds);

} // namespace spmc

#endif // IPC_CHRONO_H

