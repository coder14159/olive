#ifndef IPC_SPMC_SINK_H

#include "SPMCQueue.h"
#include "detail/SharedMemory.h"

#include <atomic>
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

};

/*
 * Helper types
 */
using SPMCSinkProcess = SPMCSink<SPMCQueue<SharedMemory::Allocator>>;
using SPMCSinkThread  = SPMCSink<SPMCQueue<std::allocator<uint8_t>>>;

}

// #include "src/SPMCSink.inl"

#endif // IPC_SPMC_SINK_H
