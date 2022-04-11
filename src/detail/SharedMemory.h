#ifndef OLIVE_DETAIL_SHARED_MEMORY_H
#define OLIVE_DETAIL_SHARED_MEMORY_H

#include <boost/algorithm/string.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/thread/tss.hpp>

#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <chrono>
#include <string>

namespace olive {

/*
 * Constants and types used in shared memory IPC
 */
namespace SharedMemory
{

/*
 * Memory allocated by the managed_shared_memory class requires additional
 * space above any user created memory for book-keeping.
 */
const size_t BOOK_KEEPING = 2048;

using SegmentManager = boost::interprocess::managed_shared_memory
                                          ::segment_manager;

// A memory allocator for producer / consumer shared memory queues
using Allocator = boost::interprocess::allocator<uint8_t, SegmentManager>;

/*
 * Single producer / single consumer shared memory queue
 *
 * TODO: wrap in a SPSCQueue class so that add addional functionality can be
 * implemented. For example the ability to allow message drops.
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
 * Reserved index values used by a producer and consumers indicating state
 */
namespace Index
{
  static constexpr
  uint8_t UnInitialised = std::numeric_limits<uint8_t>::max ();
};

inline
std::string index_to_string (uint8_t index)
{
  switch (index)
  {
    case Index::UnInitialised:
      return "Index::UnInitialised";
    default:
      return std::to_string (index);
  }
}

namespace Cursor
{
  static constexpr
  size_t UnInitialised = std::numeric_limits<size_t>::max ();
};

/*
 * Return true if the cursor value is valid
 */
inline
bool is_valid_cursor (size_t cursor)
{
  return (cursor != Cursor::UnInitialised);
}

inline
std::string cursor_to_string (size_t cursor)
{
  switch (cursor)
  {
    case Cursor::UnInitialised:
      return "Cursor::UnInitialised";
    default:
      return std::to_string (cursor);
  }
}

namespace Producer
{
  static constexpr uint8_t InvalidIndex = std::numeric_limits<uint8_t>::max ();
}

static constexpr int8_t MAX_NO_DROP_CONSUMERS_DEFAULT = 4;

static constexpr size_t CACHE_LINE_SIZE = BOOST_LOCKFREE_CACHELINE_BYTES;

using SharedMemoryMutex = boost::interprocess::interprocess_mutex;

}

#endif // OLIVE_DETAIL_SHARED_MEMORY_H
