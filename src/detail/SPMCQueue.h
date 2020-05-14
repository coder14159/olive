#ifndef IPC_DETAIL_SPMC_QUEUE_H
#define IPC_DETAIL_SPMC_QUEUE_H

#include "src/Buffer.h"
#include "src/Logger.h"
#include "src/detail/SharedMemory.h"
#include "src/detail/SPMCBackPressure.h"

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
// #include <boost/thread/tss.hpp>

#include <atomic>
#include <mutex>
#include <type_traits>
#include <vector>

namespace spmc {
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

  bool initialised ();

  /*
   * Return a reference number of bytes consumed by the consumer thread
   */
  uint64_t &consumed ();

  /*
   * Return the number of bytes consumed by the consumer thread
   */
  uint64_t consumed () const;

  /*
   * Set the number of bytes consumed by the consumer thread
   */
  void consumed (uint64_t consumed);

  /*
   * Returns true if the consumer thread permits message drops
   */
  bool message_drops_allowed () const;

  /*
   * Allow the consumer thread to drop messsages if it cannot keep up the
   * producer thread message rate
   */
  void allow_message_drops ();

};

/*
 * Inter-process bytes consumed count
 */
class SharedMemoryConsumer
{
public:
  bool initialised () { return (m_bytes != Consumer::UnInitialised); }

  /*
   * Return a reference to the number of bytes consumed by the consumer process
   */
  uint64_t &consumed ()       { return m_bytes; }

  /*
   * Return the number of bytes consumed by the consumer process
   */
  uint64_t  consumed () const { return m_bytes; }

  /*
   * Set the number of bytes consumed by the consumer process
   */
  void consumed (uint64_t consumed) { m_bytes = consumed; }

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
   * Number of bytes of data consumed
   */
  uint64_t m_bytes = Consumer::UnInitialised;

  /*
   * True is message drops are permitted for a consumer
   */
  bool     m_messageDropsAllowed = false;

};

/*
 * A queue for single producer / multiple consumers.
 *
 * Supports shared memory inter-process or inter-thread communication between
 * a producer and multiple consumers.
 */
template <class Allocator,
          size_t MaxNoDropConsumers = MAX_NO_DROP_CONSUMERS_DEFAULT>
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
   * Construct a SPMCQueue for use in-process by a single producer and multiple
   * consumers or shared between processes.
   */
  SPMCQueue (size_t capacity);

  SPMCQueue (size_t capacity, const Allocator &allocator);

  ~SPMCQueue ();

  /*
   * Return number of bytes which have been written to the queue by teh producer
   * and are available to to be consumed.
   */
  uint64_t committed () const;

  /*
   * Return the capacity of the queue
   */
  size_t capacity ();

  /*
   * Push data into the queue, always succeeds unless no drops is enabled and
   * a consumer is slow.
   *
   * The queue should be larger than the data size + header size
   */
  template <typename Header>
  bool push (const Header &header, const std::vector<uint8_t> &data);

  /*
   * Push data into the queue, always succeeds unless no drops is enabled
   * and a consumer is slow.

   * The queue should be larger than the data size + header size
   */
  template <typename Header>
  bool push (const Header &header, const uint8_t *data, size_t size);

  /*
   * Push POD data into the queue, always succeeds unless no drops is enabled
   * and a consumer is slow.

   * The queue should be larger than the data size + header size
   */
  template <typename Header, typename Data>
  bool push (const Header &header, const Data &data);

  /*
   * Pop data out of the queue, header and data must popped in a single call
   */
  template <typename Header>
  bool pop (Header               &header,
            std::vector<uint8_t> &data,
            ProducerType         &producer,
            ConsumerType         &consumer);

  /*
   * Prefetch a chunk of data for caching in a fast circular buffer
   */
  template <typename BufferType>
  bool pop (BufferType   &chunk,
            ProducerType &producer,
            ConsumerType &consumer);
  /*
   * Append queue data to a data chunk
   */
  bool pop (std::vector<uint8_t> &chunk,
            size_t                size,
            ProducerType         &producer,
            ConsumerType         &consumer);
  /*
   * Unregister a consumer thread / process
   */
  void unregister_consumer (size_t index);

  /*
   * Return true if the produce has restarted
   */
  bool producer_restarted (const ConsumerType &consumer) const;

private:

  void consumer_checks (ProducerType &producer, ConsumerType &consumer);

  void initialise_consumer (ProducerType &producer, ConsumerType &consumer);

  void copy_to_buffer (const uint8_t *from, uint8_t* to, size_t size, size_t offset = 0);

  bool copy_from_buffer (const uint8_t* from, uint8_t *to, size_t size, uint64_t &consumed,
                       bool messageDropsAllowed);

  bool copy_from_buffer (const uint8_t* from, uint8_t *to, size_t size,
                       ConsumerType &consumer);

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

  typedef typename Allocator::pointer Pointer;

  /*
   * Structure used by consumers exert back pressure on the consumer
   */
  BackPressureType m_backPressure;

  const size_t m_capacity __attribute__ ((aligned (CACHE_LINE_SIZE)));

  /*
   * Counter used to claim a data range by the producer to write data.
   *
   * Consumer threads use this counter to check if a producer has begun
   * ovewriting a range which the consumer has just read.
   */
  std::atomic<uint64_t> m_claimed
                            __attribute__ ((aligned (CACHE_LINE_SIZE))) = { 0 };

  /*
   * Counter used by the producer to publish a data range
   */
  std::atomic<uint64_t> m_committed
                            __attribute__ ((aligned (CACHE_LINE_SIZE))) = { 0 };

  /*
   * A buffer held in shared or heap memory used by the producer to pass data
   * to the consumers
   */
  Pointer m_buffer __attribute__ ((aligned (CACHE_LINE_SIZE))) = { nullptr };

};


} // namespace detail {
} // namespace spmc {

#include "src/detail/SPMCQueue.inl"

#endif // IPC_DETAIL_SPMC_QUEUE_H
