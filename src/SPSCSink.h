#ifndef IPC_SPSC_SINK_H

#include "Assert.h"
#include "Chrono.h"
#include "detail/SharedMemory.h"

#include <boost/interprocess/managed_shared_memory.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace spmc {

/*
 * Use SPSCSink to put data into the shared memory queue.
 */
class SPSCSink
{
public:
  SPSCSink (const std::string &memoryName,
            const std::string &objectName,
            size_t             queueSize);

  /*
   * Send a data packet to the shared memory queue
   */
  void next (const std::vector<uint8_t> &data);

  /*
   * Send a null message to keep the cache warm
   */
  void next_keep_warm ();

  void stop ();

  std::string name () const { return m_name; }

private:
  std::string m_name;

  SharedMemory::SPSCQueue *m_queue = { nullptr };

  uint64_t m_sequenceNumber = 0;

  alignas (CACHE_LINE_SIZE)
  std::atomic<bool> m_stop = { false };

  boost::interprocess::managed_shared_memory m_memory;

  SharedMemory::Allocator m_allocator;

  alignas (CACHE_LINE_SIZE)
  Header m_warmupHdr = {
      HEADER_VERSION,
      WARMUP_MESSAGE_TYPE,
      0,
      0,
      DEFAULT_TIMESTAMP
  };
};

}

#include "SPSCSink.inl"

#endif // IPC_SPSC_SINK_H
