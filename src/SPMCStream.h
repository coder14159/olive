#ifndef IPC_SPMC_STREAM_H

#include "SPMCQueue.h"
#include "Buffer.h"
#include "detail/SharedMemory.h"

#include <atomic>
#include <string>
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
private:
  SPMCStream (const SPMCStream &) = delete;
  SPMCStream & operator= (const SPMCStream &) = delete;

public:
  /*
   * Initialise a stream consuming from named shared memory
   */
  SPMCStream (const std::string &memoryName,
              const std::string &queueName,
              bool allowMessageDrops,
              size_t prefetchSize);

  /*
   * Initialise a stream consuming from memory shared between threads in a
   * single process.
   */
  SPMCStream (QueueType &queue,
              bool allowMessageDrops,
              size_t prefetchSize);

  ~SPMCStream ();

  /*
   * Stop retrieving data from share memory
   */
  void stop ();

  /*
   * Retrieve the next packet of data, non-blocking
   */
  template<typename Vector>
  bool next (Header &header, Vector &data);

  bool next (Header &header, Buffer<std::allocator<uint8_t>> &data);

  /*
   * Retrieve the next packet of data, non-blocking
   */
  template<typename Vector>
  bool next_non_blocking (Header &header, Vector &data);

private:

  void init (bool allowMessageDrops, size_t prefetchSize);

  /*
   * Pull data from the shared queue.
   *
   * A placeholder function for backoff strategy implementations
   */
  bool receive (Header &header, std::vector<uint8_t> &data);

private:

  bool m_registered = { false };

  std::atomic<bool> m_stop = { false };

  std::unique_ptr<QueueType> m_queuePtr;

  QueueType &m_queue;

  Buffer<std::allocator<uint8_t>> m_cache;
};

/*
 * Helper types
 */
using SPMCStreamProcess = SPMCStream<SPMCQueue<SharedMemory::Allocator>>;
using SPMCStreamThread  = SPMCStream<SPMCQueue<std::allocator<uint8_t>>>;

}

#include "SPMCStream.inl"

#endif // IPC_SPMC_STREAM_H
