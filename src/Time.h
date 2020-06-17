#ifndef IPC_TIME_H
#define IPC_TIME_H

#include <chrono>
#include <string>

namespace spmc {

using Clock        = std::chrono::steady_clock;
using TimePoint    = std::chrono::time_point<Clock>;
using Nanoseconds  = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;
using Seconds      = std::chrono::seconds;

int64_t nanoseconds_since_epoch (const TimePoint &time_point);

TimePoint timepoint_from_nanoseconds_since_epoch (int64_t nanoseconds);

} // namespace spmc

#endif // IPC_TIME_H

