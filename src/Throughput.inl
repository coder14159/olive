#include "Assert.h"
#include "Throughput.h"
#include "detail/SharedMemory.h"
#include "detail/Utils.h"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <iostream>
#include <iomanip>

namespace fs = boost::filesystem;

namespace olive {

inline
void Throughput::reset ()
{
  m_messages = 0;
  m_bytes    = 0;

  m_timer.reset ().start ();
}

inline
void Throughput::next (uint64_t bytes, uint64_t messages)
{
  if (m_stop)
  {
    return;
  }

  m_messages += messages;

  m_bytes += bytes;
}

} // namespace olive
