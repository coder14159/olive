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
  SPSCStream (const std::string &name, size_t prefetchCache);
  ~SPSCStream ();

  /*
   * Retrieve the next packet of data.
   */
  bool next (Header &header, std::vector<uint8_t> &data);

  void stop ();

private:

  size_t receive (uint8_t* data, size_t size);

private:

  boost::interprocess::managed_shared_memory m_memory;

  std::unique_ptr<SharedMemory::Allocator> m_allocator;

  SharedMemory::SPSCQueue *m_queue = { nullptr };

  std::atomic<bool> m_stop = { false };

  Buffer<std::allocator<uint8_t>> m_cache;

  bool m_cacheEnabled = true;

};

} // namespace spmc

#include "SPSCStream.inl"

#endif // IPC_SPSC_STREAM_H
