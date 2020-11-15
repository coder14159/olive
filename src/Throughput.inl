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

namespace spmc {

inline
void Throughput::reset ()
{
  m_messages = 0;
  m_bytes    = 0;
  m_dropped  = 0;

  m_timer.reset ().start ();
}

inline
void Throughput::next (uint64_t header, uint64_t payload, uint64_t seqNum)
{
  next (header + payload, seqNum);
}

inline
void Throughput::next (uint64_t bytes, uint64_t seqNum)
{
  if (m_stop)
  {
    return;
  }

  /*
   * Reset on startup or if the server was restarted
   */
  if (SPMC_EXPECT_FALSE (m_seqNum < 2 || seqNum < m_seqNum))
  {
    reset ();

    m_seqNum = seqNum;
  }

  /*
   * Check for dropped messages
   */
  auto diff = seqNum - m_seqNum;

  if (SPMC_EXPECT_FALSE (diff > 1))
  {
    m_dropped += diff;
  }

  ++m_messages;

  m_bytes += bytes;

  m_seqNum = seqNum;
}

} // namespace spmc
