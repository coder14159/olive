#include "Latency.h"

#include <chrono>
#include <string>

#include <boost/format.hpp>

namespace spmc {

template<typename DurationType>
TimeDuration::TimeDuration (const DurationType duration)
: m_nanoseconds (std::chrono::duration_cast<Nanoseconds> (duration))
{ }

} // namespace spmc
