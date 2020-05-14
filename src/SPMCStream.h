#ifndef IPC_SPMC_STREAM_H

#include "SPMCQueue.h"
#include "src/Buffer.h"
#include "src/detail/SharedMemory.h"

#include <atomic>
#include <vector>

namespace spmc {

/*
 * A single producer/ multiple consumer data stream.
 *
 * Stream shared memory data from a single producer using a shared memory queue
 * capable of supporting multiple consumers.
 *
 * If the SPMCStream is constructed to allow dropping of messages then it will
 * not exert back-pressure on the server.
 *
 */
template <typename QueueType>
class SPMCStream
{
public:
  SPMCStream (const SPMCStream &) = delete;
  SPMCStream & operator= (const SPMCStream &) = delete;

  /*
   * Initialise a stream consuming from named shared memory
   */
  SPMCStream (const std::string &memoryName,
              const std::string &queueName,
              bool allowMessageDrops,
              size_t prefetchSize);

  /*
   * Initialise a stream consuming from memory shared between threads in a
   * single process
   */
  SPMCStream (QueueType &queue,
              bool allowMessageDrops,
              size_t prefetchSize);

  ~SPMCStream ();

  /*
   * Retrieve the next packet of data, blocks until successful
   */
  bool next (Header &header, std::vector<uint8_t> &data);

  void stop ();

private:

  /*
   * Pull data from the shared queue.
   *
   * A placeholder function for backoff strategy implementations
   */
  bool receive (Header &header, std::vector<uint8_t> &data);

private:

  std::atomic<bool> m_stop = { false };

  Buffer<std::allocator<uint8_t>> m_cache;

  std::unique_ptr<QueueType> m_queueObj;
  QueueType &m_queue;
};

/*
 * Helper types
 */
using SPMCStreamProcess = SPMCStream<SPMCQueue<SharedMemory::Allocator>>;
using SPMCStreamThread  = SPMCStream<SPMCQueue<std::allocator<uint8_t>>>;

}

#include "src/SPMCStream.inl"

#endif // IPC_SPMC_STREAM_H
