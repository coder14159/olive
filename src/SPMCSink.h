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
public:
  SPMCSink (const SPMCSink &) = delete;
  SPMCSink & operator= (const SPMCSink &) = delete;

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
   * Serialise data to the queue
   */
  void next (const std::vector<uint8_t> &data);

  /*
   * Serialise POD data to the queue
   */
  template<typename POD>
  void next (const POD &data);

  /*
   * Send a header message to the queue intended to keep queue warm in the cache
   */
  void next_keep_warm ();

  void stop ();

  /*
   * Reference to the queue to be shared with SPMCStream objects for
   * inter-thread communication
   */
   QueueType& queue () { return m_queue; }

private:

  alignas (CACHE_LINE_SIZE)
  bool m_stop = { false };

  alignas (CACHE_LINE_SIZE)
  uint64_t m_sequenceNumber = 0;

  alignas (CACHE_LINE_SIZE)
  Header m_warmupHdr = {
      HEADER_VERSION,
      WARMUP_MESSAGE_TYPE,
      0,
      0,
      DEFAULT_TIMESTAMP
  };

  QueueType m_queue;
};

/*
 * Helper types
 */
using SPMCSinkProcess = SPMCSink<SPMCQueue<SharedMemory::Allocator>>;
using SPMCSinkThread  = SPMCSink<SPMCQueue<std::allocator<uint8_t>>>;

}

#include "SPMCSink.inl"

#endif // IPC_SPMC_SINK_H
