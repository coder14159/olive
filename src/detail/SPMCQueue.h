#ifndef OLIVE_DETAIL_SPMC_QUEUE_H
#define OLIVE_DETAIL_SPMC_QUEUE_H

#include "Logger.h"
#include "detail/SharedMemory.h"
#include "detail/SPMCBackPressure.h"

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include <atomic>
#include <mutex>
#include <type_traits>
#include <vector>

namespace olive {

/*
 * Use this enum to when pushing data to the shared queue.
 *
 * If more than one data structure should be contiguous in the queue when pushed
 * use AcquireSpace::YESYes
 */
enum AcquireRelease
{
  /*
   * A single push of data to the queue will acquire necessary space in the
   * queue for serialised data and make it available in a single call
   *
   * See also methods
   *
   * detail::SPMCQueue::acquire_space (..)
   * detail::SPMCQueue::release_space (..)
   */
  Yes,
  /*
   * If items are copied to the queue in a single call there is no need to
   * acquire space, push data items and then release space explicitly.
   */
  No
};

namespace detail {
/*
 * A queue for single producer / multiple consumers.
 *
 * Supports shared memory inter-process or inter-thread communication between
 * a producer and multiple consumers.
 */
template <typename Allocator,
          uint8_t MaxNoDropConsumers = MAX_NO_DROP_CONSUMERS_DEFAULT>
class SPMCQueue : private Allocator
{
private:

  SPMCQueue () = delete;
  SPMCQueue (const SPMCQueue &) = delete;

  typedef SPMCBackPressure<std::mutex, MaxNoDropConsumers>
          InprocessBackPressure;

  typedef SPMCBackPressure<SharedMemoryMutex, MaxNoDropConsumers>
          MultiProcessBackPressure;

public:

  typedef typename std::conditional<
    std::is_same<std::allocator<typename Allocator::value_type>,
                                Allocator>::value,
          InprocessBackPressure,
          MultiProcessBackPressure>::type BackPressureType;

public:
  /*
   * Construct an SPMCQueue for use in-process by a single producer thread and
   * multiple consumer threads.
   */
  SPMCQueue (size_t capacity);

  /*
   * Construct an SPMCQueue for use with a single producer and multiple
   * consumers.
   *
   * Use one of two allocator types, either the SharedMemory::Allocator if
   * communicating between processes or the std::allocator for communicating
   * between threads.
   */
  SPMCQueue (size_t capacity, const Allocator &allocator);

  ~SPMCQueue ();

  /*
   * Return a pointer to the internal buffer shared between either processes
   * or threads
   */
  uint8_t *buffer () const;
  /*
   * Register a consumer thread / process
   */
  void register_consumer (ConsumerState &consumer);
  /*
   * Unregister a consumer thread / process
   */
  void unregister_consumer (const ConsumerState &consumer);

  /*
   * Return the capacity of the queue
   */
  size_t capacity () const;

private:
  /*
   * Copy a POD type to the end of the queue.
   *
   * Space in the queue should already have been claimed by the public
   * push_variadic () method.
   *
   * Use offset if pushing multiple data items as one atomic unit with a call to
   * acquire_space before pushing the data items and then publish the data using
   * a release_space call afterwards.
   */
  template <typename T>
  constexpr size_t push_variadic_item (const T &pod, size_t offset = 0);

  /*
   * Push data from a vector to the queue
   */
  size_t push_variadic_item (const std::vector<uint8_t> &data,
                             size_t offset = 0);

public:
  /*
   * Return the size of data available to consume for a particular consumer
   */
  size_t read_available (const ConsumerState &consumer) const;

  BackPressureType &back_pressure () { return m_backPressure; }
  /*
   * Push one or more data items to the queue. The data is published for
   * consuming once all the data items have been copied to the queue.
   *
   * Space is acquired/released once for all head..tail objects.
   *
   * Currently supports POD types and classes with methods data () and size ()
   * in bytes, for example std::vector<uint8_t>
   */
  template<typename Head, typename...Tail>
  bool push_variadic (const Head &head, const Tail&...tail);

  /*
   * Copy a POD type to the end of the queue.
   *
   * If space in the queue has already been reserved using then set
   * acquire_release to No. See acquire_space () method.
   *
   * Use offset if pushing multiple data items as one atomic unit with a call to
   * acquire_space before consuming the data items and release_space call after.
   */
  template <typename POD>
  size_t push (const POD &data,
             AcquireRelease acquire_release = AcquireRelease::Yes,
             size_t offset = 0);

  /*
   * Push serialised data to the queue.
   *
   * acquire_release/offset variables are as per POD queue push
   */
  size_t push (const uint8_t *data, size_t size,
             AcquireRelease acquire_release = AcquireRelease::Yes,
             size_t offset = 0);

  /*
   * Pop a POD type from the queue
   */
  template<typename POD>
  bool pop (POD &pod, ConsumerState &consumer);

  /*
   * Pop seralised data from the queue
   */
  bool pop (uint8_t *data, size_t size, ConsumerState &consumer);

  /*
   * Prefetch a chunk of data for caching in a local non-shared circular buffer
   */
  template <typename BufferType>
  bool prefetch_to_cache (BufferType &cache, ConsumerState &consumer);
  /*
   * Return true if the producer has restarted
   */
  bool producer_restarted (const ConsumerState &consumer) const;

private:
  /*
   * Copy producer data to the internal queue
   */
  void copy_to_queue (const uint8_t *from, uint8_t* to, size_t size,
                       size_t offset = 0);
  /*
   * Copy consumer data from the internal queue to a data buffer
   */
  size_t copy_from_queue (uint8_t *to, size_t size, ConsumerState &consumer);

private:
  /*
   * Structure used by consumers exert back pressure on the producer
   */
  alignas (CACHE_LINE_SIZE)
  BackPressureType m_backPressure;
  /*
   * Local store of max size is capacity+1 for the algorithm used by the
   */
  const size_t m_maxSize = { 0 };
  /*
   * The capacity of the shared queue
   */
  const size_t m_capacity = { 0 };
  /*
   * Definition of a pointer to data accessed by either multiple threads or
   * mulitple processes
   */
  typedef typename Allocator::pointer Pointer;
  /*
   * A buffer held in shared or heap memory used by the producer to pass data
   * to the consumers
   */
  alignas (CACHE_LINE_SIZE)
  Pointer m_buffer = { nullptr };
  /*
   * A thread or process local pointer to shared memory data.
   *
   * If data is in shared memory access is faster if a pointer to the
   * de-referenced memory is stored
   */
  typedef uint8_t* LocalPointer;

  /*
   * Cache the producer buffer pointer to avoid the dereferencing cost when used
   * with shared memory
   */
  LocalPointer m_bufferProducer = { nullptr };
};

} // namespace detail {
} // namespace olive {

#include "detail/SPMCQueue.inl"

#endif // OLIVE_DETAIL_SPMC_QUEUE_H
