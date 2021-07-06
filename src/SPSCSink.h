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
template <class Allocator>
class SPSCSink
{
private:
  SPSCSink (const SPSCSink &) = delete;
  SPSCSink & operator= (const SPSCSink &) = delete;
public:
  /*
   * Create a sink object for use in a single process by multiple threads
   */
  SPSCSink (size_t capacity);

  /*
   * Create a sink object in shared memory for use by multiple processes
   */
  SPSCSink (const std::string &memoryName,
            const std::string &objectName,
            size_t             capacity);

  /*
   * Send a data packet to a shared memory queue
   */
  void next (const std::vector<uint8_t> &data);

  /*
   * Send a data packet to a shared memory queue.
   *
   * If sending to multiple queues it is more accurate to use ther same
   * timestamp for each queue.
   */
  void next (const std::vector<uint8_t> &data, TimePoint timestamp);

  /*
   * Send a null message to keep the cache warm
   */
  void next_keep_warm ();

  void stop ();

  std::string name () const { return m_name; }

private:
  std::string m_name;

  boost::interprocess::managed_shared_memory m_memory;

  SharedMemory::Allocator m_allocator;

  alignas (CACHE_LINE_SIZE)
  std::atomic<bool> m_stop = { false };

  uint64_t m_sequenceNumber = 0;

  alignas (CACHE_LINE_SIZE)
  Header m_warmupHdr = {
      HEADER_VERSION,
      WARMUP_MESSAGE_TYPE,
      0,
      0,
      DEFAULT_TIMESTAMP
  };

  SharedMemory::SPSCQueue *m_queue = { nullptr };
  SharedMemory::SPSCQueue &m_queueRef;
};

/*
 * Helper types
 */
using SPSCSinkProcess = SPSCSink<SharedMemory::Allocator>;
using SPSCSinkThread  = SPSCSink<std::allocator<uint8_t>>;

} // spmc

#include "SPSCSink.inl"

#endif // IPC_SPSC_SINK_H
