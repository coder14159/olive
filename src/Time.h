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

TimePoint timepoint_from_nanoseconds_since_epoch (int64_t nanoseconds);

} // namespace spmc

#endif // IPC_TIME_H

