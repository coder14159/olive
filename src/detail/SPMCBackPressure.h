#ifndef IPC_DETAIL_SPMC_BACK_PRESSURE_H
#define IPC_DETAIL_SPMC_BACK_PRESSURE_H

#include "detail/SharedMemory.h"

#include <array>
#include <atomic>

namespace spmc {
namespace detail {
/*
 * Class to track how much data has been consumed by a consumer
 * process or thread
 */
class ConsumerState
{
public:
  /*
   * Pointer to the raw shared queue data
   */
  const uint8_t *queue_ptr () const { return m_queue; }
  /*
   * Set pointer to the raw shared queue data
   */
  void queue_ptr (const uint8_t *queue)
  {
    m_queue = queue;
  }
  /*
   * Return true if the ConsmerState object has been registered with a producer
   */
  bool registered () const
  {
    return (m_cursor < Consumer::Reserved);
  }
  /*
   * Return the index id of the consumer - used by BackPressure class
   */
  uint8_t index () const { return m_index; }
  /*
   * Set the cursor to the currently read queue index value
   */
  void index (uint8_t index) { m_index = index; }
  /*
   * Return the value of consumer queue cursor
   */
  size_t cursor () const { return m_cursor; }
  /*
   * Set the cursor to the currently read queue index value
   */
  void cursor (size_t cursor) { m_cursor = cursor; }
  /*
   * Return true if a consumer process is allowed to drop messages
   */
  bool message_drops_allowed () const { return m_messageDropsAllowed; }
  /*
   * Allow the consumer process to drop messages if cannot keep up with the
   * message rate of the producer process
   */
  void allow_message_drops (bool allowMessageDrops) {
    m_messageDropsAllowed = allowMessageDrops;
  }

private:
  /*
   * Local pointer to the shared queue
   */
  alignas (CACHE_LINE_SIZE)
  const uint8_t *m_queue = nullptr;
  /*
   * An index to the back pressure array which holds the consumer queue cursor
   */
  uint8_t m_index = Producer::InvalidIndex;
  /*
   * The cursor points to an index of the shared queue indicating how much of
   * the produced data has been consumed
   */
  size_t m_cursor = Consumer::UnInitialised;
  /*
   * Set to true if message drops are permitted for a consumer
   */
  bool m_messageDropsAllowed = false;
};

/*
 * SPMCBackPressure manages the registration and unregistration of consumer
 * threads or processes with the queue.
 *
 * It exerts back pressure on the producer if required.
 */
template<class Mutex, uint8_t MaxNoDropConsumers = MAX_NO_DROP_CONSUMERS_DEFAULT>
class SPMCBackPressure
{
public:
  SPMCBackPressure (size_t capacity);
  /*
   * If a consumer registers successful then back-pressure is exerted on the
   * producer by all registered consumers so that message dropping is prevented.
   *
   * The consumer object passed is registered as one of the consumers in use and
   * is set to the index of the latest data pushed to the queue.
   *
   * Throws if registration fails.
   */
  void register_consumer (ConsumerState &consumer);
  /*
   * Unregister a consumer
   */
  void unregister_consumer (const ConsumerState &consumer);
  /*
   * Return the max size used in cursor index computations
   */
  size_t max_size () const { return m_maxSize; }
  /*
   * Use acquire/release space methods to atomically push more than one data
   * object onto the queue as a single contiguous unit.
   *
   * This is typically useful to atomically push header and payload data.
   *
   * See methods acquire_space (), release_space () and SPMCQueue::push ()
   *
   * Acquire space in the queue before pushing data. Publish the data using
   * the release_space () method.
   *
   * Only required for writing more than one data type as a contiguous unit in
   * the internal queue eg header and data
   */
  bool acquire_space (size_t size);
  /*
   * Release space for consuming after acquired using acquire_space () method
   */
  void release_space ();
  /*
   * Return the size of queue data available to be read from the perspective of
   * a consumers reader cursor
   */
  size_t read_available (size_t readerCursor) const;
  /*
   * Return true if there are any consumers configured not to drop messages
   */
  bool has_non_drop_consumers () const;
  /*
   * Used by the consumer to inform the producer of its progress consuming data.
   *
   * Only used by consumers configured not to drop messages.
   */
  void consumed (uint8_t readerIndex, size_t size);
  /*
   * Return the index of the committed data cursor
   */
  size_t committed_cursor () const;
  /*
   * Return the index of the committed data cursor
   */
  size_t claimed_cursor () const;
  /*
   * Return the value of a cursor advanced a number of bytes along a circular
   * buffer
   */
  size_t advance_cursor (size_t cursor, size_t advance) const;

private:
  SPMCBackPressure ();

  /*
   * Return the size of queue data which is writable for given reader and writer
   * cursors
   */
  size_t write_available (size_t readerCursor, size_t writerCursor) const;
  /*
   * Return the minimum size of queue data which is writable taking into account
   * the potentially multiple consumers
   *
   * Only called by the single writer process
   */
  size_t write_available () const;

private:
  /*
   * Current maximum value of consumer indexes
   */
  uint8_t m_maxConsumerIndex = { 0 };
  /*
   * Current number of consumers
   */
  alignas (CACHE_LINE_SIZE)
  uint8_t m_consumerCount = { 0 };
  /*
   * The queue capacity
   */
  const size_t m_maxSize = { 0 };
  /*
   * Counter used to claim a data range by the producer before writing data.
   *
   * Consumer threads use this counter to check if a producer has begun
   * ovewriting a range which the consumer has just read.
   */
  alignas (CACHE_LINE_SIZE)
  size_t m_claimed = { 0 };
  /*
   * Index used to implement fair servicing of the ConsumerArray
   * TODO: make use of this variable!!
   */
  // uint8_t m_lastIndex = { 0 };
  /*
   * Counter used by the producer to publish a data range
   */
  alignas (CACHE_LINE_SIZE)
  std::atomic<size_t> m_committed = { 0 };
  /*
   * Array holding the bytes consumed for each non message dropping consumer
   */
  alignas (CACHE_LINE_SIZE)
  std::array<size_t, MaxNoDropConsumers> m_consumers;
  /*
   * Mutex used to register/unregister new consumer threads
   */
  Mutex m_mutex;
};

} // namespace detail {
} // namespace spmc {

#include "detail/SPMCBackPressure.inl"

#endif // IPC_DETAIL_SPMC_BACK_PRESSURE_H
