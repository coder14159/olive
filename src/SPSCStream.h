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
class SPSCStream
{
public:
  SPSCStream (const std::string &name);
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

  using QueueType = SharedMemory::SPSCQueue;

  /*
   * TODO:
   * Currently inter-process communication only. Extend to be compatible with
   * inter-thread communication.
   */
  alignas (CACHE_LINE_SIZE)
  QueueType *m_queue = { nullptr };

  bool m_allowDrops = false;

  std::atomic<bool> m_stop = { false };

  alignas (CACHE_LINE_SIZE)
  std::unique_ptr<SharedMemory::Allocator> m_allocator;

  alignas (CACHE_LINE_SIZE)
  Buffer<std::allocator<uint8_t>> m_cache;

  boost::interprocess::managed_shared_memory m_memory;

};

} // namespace spmc

#include "SPSCStream.inl"

#endif // IPC_SPSC_STREAM_H
