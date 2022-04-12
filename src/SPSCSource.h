#ifndef OLIVE_SPSC_SOURCE_H
#define OLIVE_SPSC_SOURCE_H

#include "Assert.h"
#include "Chrono.h"
#include "detail/SharedMemory.h"

#include <boost/interprocess/managed_shared_memory.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace olive {

/*
 * Use SPSCSource to put data into the shared memory queue.
 */
template <class Allocator>
class SPSCSource
{
private:
  SPSCSource (const SPSCSource &) = delete;
  SPSCSource & operator= (const SPSCSource &) = delete;
public:
  /*
   * Create a sink object for use in a single process by multiple threads
   */
  SPSCSource (size_t capacity);

  /*
   * Create a sink object in shared memory for use by multiple processes
   */
  SPSCSource (const std::string &memoryName,
              const std::string &objectName,
              size_t             capacity);

  /*
   * Send a data packet to a shared memory queue
   * Blocks until successful
   */
  void next (const std::vector<uint8_t> &data);
  /*
   * Send a null message to keep the cache warm
   */
  void next_keep_warm ();

  void stop ();

  std::string name () const { return m_name; }

private:
  std::string m_name;

  boost::interprocess::managed_shared_memory m_memory;

  SharedMemory::Allocator m_allocator;

  alignas (CACHE_LINE_SIZE)
  bool m_stop = { false };

  uint64_t m_sequenceNumber = 0;

  alignas (CACHE_LINE_SIZE)
  Header m_warmupHdr = {
      HEADER_VERSION,
      WARMUP_MESSAGE_TYPE,
      0,
      0,
      DEFAULT_TIMESTAMP
  };

  std::vector<uint8_t> m_buffer;

  SharedMemory::SPSCQueue *m_queue = { nullptr };
  SharedMemory::SPSCQueue &m_queueRef;
};

/*
 * Helper types
 */
using SPSCSourceProcess = SPSCSource<SharedMemory::Allocator>;
using SPSCSourceThread  = SPSCSource<std::allocator<uint8_t>>;

} // namespace olive

#include "SPSCSource.inl"

#endif // OLIVE_SPSC_SOURCE_H
