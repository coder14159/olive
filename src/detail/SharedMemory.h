#ifndef IPC_DETAIL_SHARED_MEMORY_H
#define IPC_DETAIL_SHARED_MEMORY_H

#include <boost/algorithm/string.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/thread/tss.hpp>

#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <chrono>
#include <string>

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

/*
 * Single producer / single consumer shared memory queue
 *
 * TODO: wrap in a SPSCQueue object to add addional functionality like, for
 * example add ability to allow message drops.
 */
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
static const uint8_t HEADER_VERSION = 1;

static const uint8_t STANDARD_MESSAGE_TYPE = 0;
static const uint8_t WARMUP_MESSAGE_TYPE   = 1;

static const int64_t DEFAULT_TIMESTAMP = std::numeric_limits<int64_t>::min ();

struct Header
{
  uint8_t  version   = HEADER_VERSION;
  uint8_t  type      = STANDARD_MESSAGE_TYPE;
  size_t   size      = 0;
  uint64_t seqNum    = 0;
  int64_t  timestamp = DEFAULT_TIMESTAMP;
};

/*
 * Definitions used by a consumer threads / processes to change state
 */
namespace Consumer
{
  static constexpr size_t Ready         = std::numeric_limits<size_t>::max ();
  static constexpr size_t UnInitialised = std::numeric_limits<size_t>::max () - 1;
  static constexpr size_t Stopped       = std::numeric_limits<size_t>::max () - 2;
  static constexpr size_t Reserved      = std::numeric_limits<size_t>::max () - 3;
};

namespace Producer
{
  static constexpr uint8_t InvalidIndex = std::numeric_limits<uint8_t>::max ();
}

static constexpr int8_t MAX_NO_DROP_CONSUMERS_DEFAULT = 4;

static constexpr size_t CACHE_LINE_SIZE = BOOST_LOCKFREE_CACHELINE_BYTES;

using SharedMemoryMutex = boost::interprocess::interprocess_mutex;

}

#endif // IPC_DETAIL_SHARED_MEMORY_H
