#ifndef IPC_DETAIL_SHARED_MEMORY_H
#define IPC_DETAIL_SHARED_MEMORY_H

#include <boost/algorithm/string.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/thread/tss.hpp>

//#include "patch/lockfree/spsc_queue.hpp"
#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <chrono>
#include <string>

//#define SPMC_TRACE_ENABLE 1

namespace spmc {

/*
 * Constants and types used in shared memory IPC
 */
namespace SharedMemory
{

/*
 * Memory allocated by the managed_shared_memory class requires additional
 * 272 bytes above any user created memory for book-keeping.
 *
 * Round the number up a bit to be on the safe side.
 */
const size_t BOOK_KEEPING = 2048;

using SegmentManager = boost::interprocess::managed_shared_memory
                                          ::segment_manager;

// A memory allocator for producer / consumer shared memory queues
using Allocator = boost::interprocess::allocator<uint8_t, SegmentManager>;

// Single producer / single consumer shared memory queue
using SPSCQueue = boost::lockfree::spsc_queue<uint8_t,
                                              boost::lockfree
                                                   ::allocator<Allocator>>;

using Counter = std::atomic<int>;

}

/*
 * A message header for streaming shared memory data
 *
 * Not using a "packed" structure for the header as this prevents some memory
 * access optimisation and is measurable slower.
 *
 * In a latency test at 30K messages/sec, the 99.9th percentile rose from
 * 19.44 us to 52.94 us when using a packed structure.
 *
 */
struct Header
{
  uint8_t  version   = 1;
  uint8_t  type      = 0;
  size_t   size      = 0;
  uint64_t seqNum    = 0;
  int64_t  timestamp = std::numeric_limits<int64_t>::min ();
};

/*
 * Definitions used by a consumer thread / process
 */
struct Consumer
{
  static constexpr size_t   UnInitialisedIndex = std::numeric_limits<size_t>::max ();
  static constexpr uint64_t UnInitialised = std::numeric_limits<uint64_t>::max ();
  static constexpr uint64_t Stopped       = std::numeric_limits<uint64_t>::max () - 1;
};

static constexpr size_t MAX_NO_DROP_CONSUMERS_DEFAULT = 64;

static constexpr size_t CACHE_LINE_SIZE = BOOST_LOCKFREE_CACHELINE_BYTES;

using SharedMemoryMutex = boost::interprocess::interprocess_mutex;

}

#endif // IPC_DETAIL_SHARED_MEMORY_H
