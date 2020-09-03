#include "Chrono.h"
#include "Logger.h"

#include <boost/cstdint.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include <algorithm>
#include <sstream>

namespace ba = boost::accumulators;
namespace fs = boost::filesystem;

namespace spmc {

inline
void Latency::reset ()
{
  m_quantiles = m_empty;
  m_min       = Nanoseconds::max ();
  m_max       = Nanoseconds::min ();
}

inline
void Latency::next (Nanoseconds nanoseconds)
{
  if (m_stop)
  {
    return;
  }

  for (auto &quantile : m_quantiles)
  {
    quantile.second (nanoseconds.count ());
  }

  m_min = std::min (m_min, nanoseconds);
  m_max = std::max (m_max, nanoseconds);
}

} // namespace spmc
