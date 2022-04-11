#ifndef OLIVE_SPSC_STREAM_H

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

namespace olive {

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
  SPSCStream (const std::string &memoryName, size_t prefetchSize = 0);

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
  bool pop (uint8_t* to, size_t size);

  bool prefetch_to_cache ();

  bool check_cache_size (size_t size);

  /*
   * Pop header and data from the cache
   */
  template<class Header, class Data>
  bool pop_from_cache (Header &header, Data &data);

  size_t copy_from_queue (uint8_t* to, size_t size);

private:

  bool m_stop = { false };

  using QueueType = SharedMemory::SPSCQueue;

  /*
   * TODO:
   * Currently inter-process communication only. Extend to be compatible with
   * inter-thread communication.
   */
  alignas (CACHE_LINE_SIZE)
  QueueType *m_queuePtr = { nullptr };

  Buffer<std::allocator<uint8_t>> m_cache;

  alignas (CACHE_LINE_SIZE)
  std::unique_ptr<SharedMemory::Allocator> m_allocator;

  boost::interprocess::managed_shared_memory m_memory;

};

/*
 * Helper types
 */
using SPSCStreamProcess = SPSCStream<SharedMemory::Allocator>;
using SPSCStreamThread  = SPSCStream<std::allocator<uint8_t>>;

} // namespace olive

#include "SPSCStream.inl"

#endif // OLIVE_SPSC_STREAM_H
