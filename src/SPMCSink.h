#ifndef OLIVE_SPMC_SINK_H

#include "SPMCQueue.h"
#include "detail/SharedMemory.h"

#include <atomic>
#include <string>
#include <vector>

namespace olive {

/*
 * A single producer/ multiple consumer data stream.
 *
 * Stream shared memory data from a single producer using a shared memory queue
 * capable of supporting multiple consumers.
 *
 * If the SPMCSink is constructed to allow dropping of messages then it will
 * not exert back-pressure on the server.
 *
 */
template <typename QueueType>
class SPMCSink
{
private:
  SPMCSink (const SPMCSink &) = delete;
  SPMCSink & operator= (const SPMCSink &) = delete;

public:
  /*
   * Initialise a stream consuming from named shared memory
   */
  SPMCSink (const std::string &memoryName, const std::string &queueName);

  /*
   * Initialise a stream consuming from memory shared between threads in a
   * single process.
   */
  SPMCSink (QueueType &queue);

  ~SPMCSink ();

  /*
   * Stop retrieving data from share memory
   */
  void stop ();

  /*
   * Retrieve the next packet of data, non-blocking
   */
  template<typename Vector>
  bool next (Header &header, Vector &data);

  /*
   * Retrieve the next packet of data, non-blocking
   */
  template<typename Vector>
  bool next_non_blocking (Header &header, Vector &data);

private:
  /*
   * Pull data from the shared queue.
   *
   * A placeholder function for backoff strategy implementations
   */
  bool receive (Header &header, std::vector<uint8_t> &data);

private:

  bool m_stop = { false };

  detail::ConsumerState m_consumer;

  std::unique_ptr<QueueType> m_queuePtr;

  QueueType &m_queue;
};

/*
 * Helper types
 */
using SPMCSinkProcess = SPMCSink<SPMCQueue<SharedMemory::Allocator>>;
using SPMCSinkThread  = SPMCSink<SPMCQueue<std::allocator<uint8_t>>>;

}

#include "SPMCSink.inl"

#endif // OLIVE_SPMC_SINK_H
