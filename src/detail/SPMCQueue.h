#ifndef IPC_DETAIL_SPMC_QUEUE_H
#define IPC_DETAIL_SPMC_QUEUE_H

#include "Buffer.h"
#include "Logger.h"
#include "detail/SharedMemory.h"
#include "detail/SPMCBackPressure.h"

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include <atomic>
#include <mutex>
#include <type_traits>
#include <vector>

namespace spmc {

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
   * detail::SPMCQueue::acquire_space (..)  and
   * detail::SPMCQueue::release_space (..)
   */
  Yes,
  /*
   * If items are copied to the queue in a single call there is no need to
   * acquire space, push data items and then release space explcitly.
   */
  No
};

namespace detail {

/*
 * A cache tracking how much data has been consumed by each thread.
 *
 * The producer uses the information to prevent consumers dropping data.
 */
class InprocessProducer
{
public:
  ~InprocessProducer ();

  size_t index () const;

  /*
   * Set the index of a producer for use by the SPMCBackPressure class
   */
  void index (size_t index);

private:

  /*
   * Thread local index used by non-message dropping consumer threads
   */
  mutable boost::thread_specific_ptr<size_t> m_index;
};

/*
 * A cache tracking how much data has been consumed by each process.
 *
 * The producer uses the information to prevent consumers dropping data.
 */
class SharedMemoryProducer
{
public:

  size_t index () const     { return m_index; }
  /*
   * Set the index of a producer for use by the SPMCBackPressure class
   */
  void index (size_t index) { m_index = index; }


private:
  /*
   * Shared memory index used by non-message dropping consumer processes
   */
  size_t m_index = Consumer::UnInitialisedIndex;
};

/*
 * In-process bytes consumed count
 */
class InprocessConsumer
{
public:
  ~InprocessConsumer ();

  const uint8_t *queue_ptr () const { return m_queue; }

  void queue_ptr (const uint8_t *queue) { m_queue = queue; }

  bool initialised () const;

  /*
   * Return the number of bytes consumed by the consumer thread
   */
  uint64_t consumed () const;

  /*
   * Set the number of bytes consumed by the consumer thread
   */
  void consumed (uint64_t consumed);

  /*
   * Add a number of bytes to the consumer count
   */
  void add (uint64_t consumed);

  /*
   * Returns true if the consumer thread permits message drops
   */
  bool message_drops_allowed () const;

  /*
   * Allow the consumer thread to drop messsages if it cannot keep up the
   * producer thread message rate
   */
  void allow_message_drops ();

private:

  const uint8_t *m_queue = { nullptr };

};

/*
 * Inter-process shared memory bytes consumed count
 */
class SharedMemoryConsumer
{
public:

  const uint8_t *queue_ptr () const { return m_queue; }

  void queue_ptr (const uint8_t *queue) { m_queue = queue; }

  bool initialised () const { return (m_bytes != Consumer::UnInitialised); }

  /*
   * Return the number of bytes consumed by the consumer process
   */
  uint64_t consumed () const { return m_bytes; }

  /*
   * Set the number of bytes consumed by the consumer process
   */
  void consumed (uint64_t consumed) { m_bytes = consumed; }

  /*
   * Add a number of bytes to the consumer count
   */
  void add (uint64_t consumed) { m_bytes += consumed; }

  /*
   * Return true if a consumer process is allowed to drop messages
   */
  bool message_drops_allowed () const { return m_messageDropsAllowed; }

  /*
   * Allow the consumer process to drop messages if cannot keep up with the
   * message rate of the producer process
   */
  void allow_message_drops () { m_messageDropsAllowed = true; }

private:

  /*
   * True is message drops are permitted for a consumer
   */
  bool m_messageDropsAllowed = false;

  const uint8_t *m_queue = nullptr;

  /*
   * Number of bytes of data consumed
   */
  uint64_t m_bytes = Consumer::UnInitialised;

};

/*
 * A queue for single producer / multiple consumers.
 *
 * Supports shared memory inter-process or inter-thread communication between
 * a producer and multiple consumers.
 */
template <typename Allocator,
          uint16_t MaxNoDropConsumers = MAX_NO_DROP_CONSUMERS_DEFAULT>
class SPMCQueue : private Allocator
{
private:
  static_assert (std::is_same<typename Allocator::value_type, uint8_t>::value,
                 "Invalid allocator value_type");

  SPMCQueue (const SPMCQueue &) = delete;

public:

  typedef typename std::conditional<
          std::is_same<std::allocator<uint8_t>, Allocator>::value,
          InprocessProducer,
          SharedMemoryProducer>::type ProducerType;

  typedef typename std::conditional<
          std::is_same<std::allocator<uint8_t>, Allocator>::value,
          InprocessConsumer,
          SharedMemoryConsumer>::type ConsumerType;

public:
  /*
   * Construct an SPMCQueue for use in-process by a single producer and multiple
   * consumer threads.
   */
  SPMCQueue (size_t capacity);

  /*
   * Construct an SPMCQueue for use with a single producer and multiple
   * consumers which can be shared between processes by using an allocator type
   * of SharedMemory::Allocator
   */
  SPMCQueue (size_t capacity, const Allocator &allocator);

  ~SPMCQueue ();

  /*
   * Return a pointer to the internal buffer shared between either processes
   * or threads
   */
  uint8_t *buffer () const;

  /*
   * Return the size of data in the queue which is unconsumed by a given
   * consumer
   */
  size_t size (const ConsumerType &consumer) const;

  /*
   * Unregister a consumer thread / process
   */
  void unregister_consumer (size_t index);

  /*
   * Acquire a chunk of queue data for writing
   *
   * Returns false free data chunk isSpaceAcquired
   *
   * Return number of bytes which have been written to the queue by the producer
   * and are available to to be consumed.
   */
  uint64_t committed () const;

  /*
   * Return the capacity of the queue
   */
  size_t capacity () const;

  void consumer_checks (ProducerType &producer, ConsumerType &consumer);

  /*
   * Use acquire/release space methods to atomically push more than one data
   * object onto the queue as a single contiguous unit.
   *
   * This is typically useful to atomically push header and payload data.
   *
   * See methods acquire_space (), release_space () and push ()
   *
   * Acquire space in the queue before pushing data. Publish the data using
   * the release_space () method.
   *
   * Only required for writing more than one data type as a contiguous unit in
   * the internal queue eg header and data
   */
  bool acquire_space (size_t size);

  /*
   * Release space for consuming after being acquired via acquire_space
   */
  void release_space ();

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
  size_t push_variadic_item (const T &pod, size_t offset = 0);

  /*
   * Push data from a vector to the queue
   */
  size_t push_variadic_item (const std::vector<uint8_t> &data,
                             size_t offset = 0);

public:
  /*
   * Push one or more data items to the queue. The data is published for
   * consuming once all the data items have been copied to the queue.
   */
  template<typename Head, typename...Tail>
  bool push_variadic (const Head &head, const Tail&...tail);

  /*
   * Copy a data array to the end of the queue
   *
   * If space in the queue has already been reserved using then set
   * space_acquired to true. See acquire_space () method.
   *
   * Use offset if pushing multiple data items as one atomic unit with a call to
   * acquire_space before consuming the data items and release_space call after.
   */
  template <typename POD>
  size_t push (const POD &data,
             AcquireRelease acquire_release = AcquireRelease::Yes,
             size_t offset = 0);

  size_t push (const uint8_t *data, size_t size,
             AcquireRelease acquire_release = AcquireRelease::Yes,
             size_t offset = 0);
public:

  template<typename POD>
  bool pop (POD &pod, ProducerType &producer, ConsumerType &consumer);

  bool pop (uint8_t *buffer, size_t size,
            ProducerType &producer, ConsumerType &consumer);
  /*
   * Pop data out of the queue, header and data must popped in a single call
   */
  template <typename Header, typename Buffer>
  bool pop (Header        &header,
            Buffer        &data,
            ProducerType  &producer,
            ConsumerType  &consumer,
            const uint8_t *buffer);
  /*
   * Pop data of specified size out of the queue into a data buffer
   */
  template <typename Buffer>
  bool pop (Buffer        &data,
            size_t         size,
            ProducerType  &producer,
            ConsumerType  &consumer,
            const uint8_t *buffer);
  /*
   * Prefetch a chunk of data for caching in a local non-shared circular buffer
   */
  template <typename BufferType>
  bool prefetch_to_cache (BufferType &cache,
            ProducerType &producer,
            ConsumerType &consumer);
  /*
   * Return true if the producer has restarted
   */
  bool producer_restarted (const ConsumerType &consumer) const;

  void print_debug () const;

private:
  void reset_producer ();

  void initialise_consumer (ProducerType &producer, ConsumerType &consumer);

  /*
   * Push data to the internal queue
   */
  void copy_to_buffer (const uint8_t *from, uint8_t* to, size_t size,
                       size_t offset = 0);

  size_t copy_from_buffer (uint8_t *to, size_t size, ConsumerType &consumer);

  template <typename Buffer>
  bool copy_from_buffer (const uint8_t* from, Buffer &to, size_t size,
                         ConsumerType &consumer);

private:

  static_assert (std::is_same<typename Allocator::value_type, uint8_t>::value,
                 "value_type for the allocator must be uint8_t");

  typedef typename std::conditional<
          std::is_same<std::allocator<uint8_t>, Allocator>::value,
          SPMCBackPressure<std::mutex,
                           MaxNoDropConsumers>,
          SPMCBackPressure<SharedMemoryMutex,
                           MaxNoDropConsumers>>::type BackPressureType;

  /*
   * Pointer to data in either local memory or shared memory
   */
  typedef typename Allocator::pointer Pointer;

  const size_t m_capacity;

  /*
   * Structure used by consumers exert back pressure on the producer
   */
  BackPressureType m_backPressure;

  /*
   * A local pointer to data either shared memory or local memory data.
   *
   * An optimisation if data is in shared memory.
   */
  typedef uint8_t* LocalPointer;

  /*
   * A buffer held in shared or heap memory used by the producer to pass data
   * to the consumers
   */
  alignas (CACHE_LINE_SIZE)
  Pointer m_buffer = { nullptr };

  /*
   * Counter used to claim a data range by the producer before writing data.
   *
   * Consumer threads use this counter to check if a producer has begun
   * ovewriting a range which the consumer has just read.
   */
  alignas (CACHE_LINE_SIZE)
  std::atomic<uint64_t> m_claimed = { 0 };

  /*
   * Counter used by the producer to publish a data range
   */
  alignas (CACHE_LINE_SIZE)
  std::atomic<uint64_t> m_committed = { 0 };

  /*
   * Cache the producer buffer pointer to avoid the dereferencing cost when used
   * with shared memory
   */
  alignas (CACHE_LINE_SIZE)
  LocalPointer m_bufferProducer = { nullptr };
};

} // namespace detail {
} // namespace spmc {

#include "detail/SPMCQueue.inl"

#endif // IPC_DETAIL_SPMC_QUEUE_H
