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
   * Send a packet of data, blocks until successful
   */
  void next (const std::vector<uint8_t> &data);

  /*
   * Send data of POD type
   */
  template<typename POD>
  void next (const POD &data);

  /*
   * Send a null message to keep the cache warm
   */
  void next_keep_warm ();

  void stop ();

  /*
   * Reference to the queue to be shared with SPMCStream objects for
   * inter-thread communication
   */
   QueueType& queue () { return m_queue; }

private:

  uint64_t m_sequenceNumber = 0;

  std::atomic<bool> m_stop = { false };

  QueueType m_queue;

  Header m_warmupHdr {
      HEADER_VERSION,
      WARMUP_MESSAGE_TYPE,
      WARMUP_MESSAGE_SIZE,
      0,
      DEFAULT_TIMESTAMP
  };

  uint8_t m_warmupMsg[WARMUP_MESSAGE_SIZE];
};

/*
 * Helper types
 */
using SPMCSinkProcess = SPMCSink<SPMCQueue<SharedMemory::Allocator>>;
using SPMCSinkThread  = SPMCSink<SPMCQueue<std::allocator<uint8_t>>>;

}

#include "SPMCSink.inl"

#endif // IPC_SPMC_SINK_H
