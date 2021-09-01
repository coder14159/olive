#ifndef IPC_DETAIL_SPMC_BACK_PRESSURE_H
#define IPC_DETAIL_SPMC_BACK_PRESSURE_H

#include "detail/SharedMemory.h"

#include <array>
#include <atomic>

namespace spmc {
namespace detail {
/*
 * Class to track how much data has been consumed by a consumer process
 * or thread.
 */
class ConsumerState
{
public:
  /*
  * Updating the a range of data which can be consumed is relatively expensive
  * as updates need to be fed back to the data producer.
  *
  * DataRange requests a chunk of data for a client to consume in a single call.
  * The client then consumes the data chunk without further interaction with the
  * producer.
  */
  class DataRange
  {
  public:
    // Return the current size of data which can be read from the queue
    bool empty () const { return m_readAvailable == 0; }

    // Return the current size of data which can be read from the queue
    size_t read_available () const { return m_readAvailable; }

    // Reset size of consumable data after available data has been consumed
    void read_available (size_t size)
    {
      m_consumed = 0;
      m_readAvailable = size;
    }

    // Return size of data currently consumed
    size_t consumed () const { return m_consumed; }

    // Update the range with the size of data which has been consumed
    void consumed (size_t size)
    {
      m_consumed += size;
      m_readAvailable -= size;
    }

  private:
    size_t m_consumed = 0;
    size_t m_readAvailable = 0;
  };

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
   * Return true if the ConsumerState object has been registered with a producer
   */
  bool registered () const { return (m_index != Index::UnInitialised); }
  /*
   * Each registered consumer has a different index value
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
   * Return the data range object defining the currently consumable data range
   */
  ConsumerState::DataRange &data_range () { return m_dataRange; }
  /*
   * Return a read-only data defining the currently consumable data range
   */
  const ConsumerState::DataRange &data_range () const { return m_dataRange; }

private:
  /*
   * Local pointer to the shared queue
   */
  const uint8_t *m_queue = nullptr;
  /*
   * An index to the back pressure array which holds the consumer queue cursor
   */
  alignas (CACHE_LINE_SIZE)
  uint8_t m_index = Index::UnInitialised;
  /*
   * The cursor points to an index of the shared queue indicating how much of
   * the produced data has been consumed
   */
  size_t m_cursor = Cursor::UnInitialised;

  DataRange m_dataRange;
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
   * Update local consumer state and the state shared with the producer
   */
  void update_consumer_state (ConsumerState &consumer);
  /*
   * Return the size of queue data available to be read from the perspective of
   * a consumers reader cursor
   */
  size_t read_available (const ConsumerState &consumer) const;

  /*
   * Return the minimum size of queue data which is writable taking into account
   * all of the consumers
   */
  size_t write_available ();
  /*
   * Return the index of the committed data cursor
   */
  size_t committed_cursor () const;
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

private:
  /*
   * Index used to implement fair servicing of the ConsumerArray
   * TODO: make use of this variable!!
   */
  uint8_t m_consumerIndex = { 0 };
  /*
   * Current maximum value of consumer indexes
   * Used during consumer registration
   */
  uint8_t m_maxConsumerIndex = { 0 };
  /*
   * Current number of consumers
   * Should be atomic or is the mutex synchronise adequate??
   */
  alignas (CACHE_LINE_SIZE)
  std::atomic<uint8_t> m_maxConsumers = { 0 };
  /*
   *
   * Queue capacity + 1
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
   * Counter used by the producer to publish a data range
   */
  alignas (CACHE_LINE_SIZE)
  std::atomic<size_t> m_committed = { 0 };
  /*
   * Array holding the bytes consumed for each non message dropping consumer
   */
  alignas (CACHE_LINE_SIZE)
  std::array<size_t, MaxNoDropConsumers> m_consumerIndexes;
  /*
   * Mutex used to register and unregister consumer threads
   */
  Mutex m_mutex;
};

} // namespace detail {
} // namespace spmc {

#include "detail/SPMCBackPressure.inl"

#endif // IPC_DETAIL_SPMC_BACK_PRESSURE_H
