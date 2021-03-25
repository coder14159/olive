#ifndef IPC_SPSC_STREAM_H

#include "Assert.h"
#include "Buffer.h"
#include "Logger.h"
#include "detail/SharedMemory.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/log/trivial.hpp>

#include <atomic>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>

namespace spmc {

/*
 * Stream shared memory data from a single producer / single consumer queue
 */
template <typename Allocator>
class SPSCStream
{
private:
  SPSCStream (const SPSCStream &) = delete;
  SPSCStream & operator= (const SPSCStream &) = delete;

public:
  SPSCStream (const std::string &memoryName);
  ~SPSCStream ();

  /*
   * Retrieve the next packet of data.
   */
  bool next (Header &header, std::vector<uint8_t> &data);

  void stop ();

private:

  template<typename POD>
  bool pop (POD &data);

  /*
   * Pop data off the queue to the location pointed to by data
   */
  bool pop (uint8_t* data, size_t size);

private:

  std::atomic<bool> m_stop = { false };

  using QueueType = SharedMemory::SPSCQueue;

  /*
   * TODO:
   * Currently inter-process communication only. Extend to be compatible with
   * inter-thread communication.
   */
  alignas (CACHE_LINE_SIZE)
  QueueType *m_queuePtr = { nullptr };

  alignas (CACHE_LINE_SIZE)
  std::unique_ptr<SharedMemory::Allocator> m_allocator;

  boost::interprocess::managed_shared_memory m_memory;

};

/*
 * Helper types
 */
using SPSCStreamProcess = SPSCStream<SharedMemory::Allocator>;
using SPSCStreamThread  = SPSCStream<std::allocator<uint8_t>>;

} // namespace spmc

#include "SPSCStream.inl"

#endif // IPC_SPSC_STREAM_H
