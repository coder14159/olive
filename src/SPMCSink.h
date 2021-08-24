#ifndef IPC_SPMC_SINK_H

#include "SPMCQueue.h"
#include "detail/SharedMemory.h"

#include <atomic>
#include <string>
#include <vector>

namespace spmc {

/*
 * A single producer/ multiple consumer data sink.
 *
 * Use SPMCSink to put data into the shared memory queue.
 */
template <typename QueueType>
class SPMCSink
{
private:
  SPMCSink (const SPMCSink &) = delete;
  SPMCSink & operator= (const SPMCSink &) = delete;

public:
  /*
   * Create a sink object for use in a single process by multiple threads
   */
  SPMCSink (size_t capacity);

  /*
   * Create a sink object in shared memory for use by multiple processes
   */
  SPMCSink (const std::string &memoryName,
            const std::string &queueName,
            size_t             capacity);

  /*
   * Stop sink sending data
   */
  void stop ();

  /*
   * Serialise data to the queue
   * Blocks until successful
   */
  void next (const std::vector<uint8_t> &data);

  /*
   * Serialise POD data to the queue
   * Blocks until successful
   */
  template<typename POD>
  void next (const POD &data);

  /*
   * Send a header message to the queue intended to keep queue warm in the cache
   */
  void next_keep_warm ();

  /*
   * Reference to the queue to be shared with SPMCStream objects for
   * inter-thread communication
   */
   QueueType& queue () { return m_queue; }

private:

  alignas (CACHE_LINE_SIZE)
  QueueType m_queue;

  alignas (CACHE_LINE_SIZE)
  std::atomic<bool> m_stop = { false };

  uint64_t m_sequenceNumber = 0;

  alignas (CACHE_LINE_SIZE)
  const Header m_warmupHdr = {
      HEADER_VERSION,
      WARMUP_MESSAGE_TYPE,
      0,
      0,
      DEFAULT_TIMESTAMP
  };
};

/*
 * Helper types
 */
using SPMCSinkProcess = SPMCSink<SPMCQueue<SharedMemory::Allocator>>;
using SPMCSinkThread  = SPMCSink<SPMCQueue<std::allocator<uint8_t>>>;

}

#include "SPMCSink.inl"

#endif // IPC_SPMC_SINK_H
