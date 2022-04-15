#include "Latency.h"

#include <chrono>
#include <string>

#include <boost/format.hpp>

namespace olive {

inline
TimeDuration::TimeDuration ()
: m_nanoseconds (Nanoseconds::zero ())
{ }

template<typename DurationType>
TimeDuration::TimeDuration (const DurationType duration)
: m_nanoseconds (std::chrono::duration_cast<Nanoseconds> (duration))
{ }

} // namespace olive
