#include "GetSize.h"
#include "Utils.h"
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>

namespace spmc {
namespace detail {

template <typename Allocator, uint16_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_backPressure (capacity)
, m_maxSize (m_backPressure.max_size ())
, m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
, m_bufferProducer (buffer ())
{
  ASSERT (m_capacity > 0, "Invalid capacity");
  ASSERT (m_buffer != nullptr, "Invalid buffer");

  std::fill (m_bufferProducer, m_bufferProducer + m_capacity, 0);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  size_t capacity, const Allocator &allocator)
: Allocator  (allocator)
, m_backPressure (capacity)
, m_maxSize (m_backPressure.max_size ())
, m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
, m_bufferProducer (buffer ())
{
  ASSERT (m_capacity > 0, "Invalid capacity");

  std::fill (m_bufferProducer, m_bufferProducer + m_capacity, 0);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::~SPMCQueue ()
{
  /*
   * This destructor is only invoked by the single process multi-threaded queue
   *
   * The interprocess queue is deallocated when the named shared memory is
   * removed.
   */
  Allocator::deallocate (m_buffer, m_capacity);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::register_consumer (
  ConsumerState &consumer)
{
  /*
   * Store a local pointer to the shared memory data buffer
   */
  consumer.queue_ptr (buffer ());
  /*
    * Register a consumer thread
    */
  if (consumer.index () == Producer::InvalidIndex)
  {
    m_backPressure.register_consumer (consumer);
  }
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  const ConsumerState &consumer)
{
  m_backPressure.unregister_consumer (consumer);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
uint8_t *SPMCQueue<Allocator, MaxNoDropConsumers>::buffer () const
{
  return reinterpret_cast<uint8_t*> (&*m_buffer);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::capacity () const
{
  return m_capacity;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::read_available (
  const ConsumerState &consumer) const
{
  return m_backPressure.read_available (consumer.cursor ());
}


template <typename Allocator, uint16_t MaxNoDropConsumers>
template <typename T>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::push_variadic_item (
  const T &pod,
  size_t offset)
{
  static_assert (std::is_trivially_copyable<T>::value,
                 "POD type must be trivially copyable");

  return push (reinterpret_cast<const uint8_t*>(&pod), sizeof (T),
               AcquireRelease::No, offset);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::push_variadic_item (
  const std::vector<uint8_t> &data,
  size_t offset)
{
  return push (data.data (), data.size (), AcquireRelease::No, offset);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
template<typename Head, typename...Tail>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push_variadic (
  const Head &head, const Tail&...tail)
{
  if (SPMC_EXPECT_TRUE (m_backPressure.acquire_space (
                          get_size (head, tail...))))
  {
    push_variadic_item (tail..., push_variadic_item (head));

    m_backPressure.release_space ();

    return true;
  }

  return false;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
template <typename POD>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const POD &pod,
  AcquireRelease acquire_release,
  size_t offset)
{
  static_assert (std::is_trivially_copyable<POD>::value,
                 "POD type must be trivially copyable");

  return push (reinterpret_cast<const uint8_t*>(&pod), sizeof (POD),
               acquire_release, offset);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const uint8_t *data,
  size_t size,
  AcquireRelease acquire_release,
  size_t offset)
{
  assert (size <= m_capacity);

  /*
   * Claim a data range of the queue to overwrite with a header and the data.
   *
   * There is only one producer so there should be no ABA issues updating the
   * claimed variable.
   */
  if (acquire_release == AcquireRelease::Yes &&
      m_backPressure.acquire_space (size) == false)
  {
    return 0;
  }
  /*
   * Copy the header and payload data to the shared buffer.
   */
  copy_to_buffer (data, m_bufferProducer, size, offset);
  /*
   * Make data available to the consumers.
   *
   * Use a release commit so the data stored cannot be ordered after the commit
   * has been made.
   */
  if (acquire_release == AcquireRelease::Yes)
  {
    m_backPressure.release_space ();
  }

  return size;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
template <typename POD>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
    POD &pod,
    ConsumerState &consumer)
{
  return pop (reinterpret_cast<uint8_t*> (&pod), sizeof (POD), consumer);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
    uint8_t* data,
    size_t size,
    ConsumerState &consumer)
{
  /*
   * Check to see if new data is available. Avoid using synchronisation if no
   * new data is available.
   *
   * Return false if data is not yet available in the queue
   */
  const size_t cursor = consumer.cursor ();

  if (SPMC_EXPECT_TRUE (m_backPressure.read_available (cursor) >= size))
  {
    /*
    * Cache a variable for the duration of the call for a small performance
    * improvement, particularly for the in-process consumer as thread-local
    * variables are a hotspot
    */
    copy_from_buffer (data, size, consumer);
    /*
    * Update back-pressure against the producer
    */
    m_backPressure.consumed (consumer.index (), size);
    /*
    * Move the current consumer cursor to a new index
    */
    consumer.cursor (m_backPressure.advance_cursor (cursor, size));

    return true;
  }

  return false;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
template <typename BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::prefetch_to_cache (
  BufferType   &cache,
  ConsumerState &consumer)
{
#pragma GCC warning "prefetch_to_cache does not work now..remove?"
  (void)cache;
  (void)consumer;
#if IGNORE_
  ASSERT (consumer.message_drops_allowed () == false,
          "Consumer message drops facility should not be enabled");

  auto consumed = consumer.consumed ();

  size_t available = m_committed.load (std::memory_order_relaxed) - consumed;

  if (available == 0)
  {
    return false;
  }

  /*
   * Append as much available data as possible to the cache
   */
  size_t size = std::min (cache.capacity () - cache.size (), available);

  m_committed.load (std::memory_order_acquire);

  if (copy_from_buffer (consumer.queue_ptr (), cache, size, consumer))
  {
    m_backPressure.consumed (consumer.index (), size);

    return true;
  }
#endif
  return false;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::copy_to_buffer (
  const uint8_t* from, uint8_t* to, size_t size, size_t offset)
{
  // TODO hoist out of copy_to_buffer method?
  size_t writerCursor = m_backPressure.committed_cursor ();

  if (offset > 0)
  {
    writerCursor = m_backPressure.advance_cursor (writerCursor, offset);
  }

  if ((writerCursor + size) > m_maxSize)
  {
    /*
     * Copying data wraps over the end of the buffer
     */
    size_t spaceToEnd = m_maxSize - writerCursor;

    std::memcpy (to + writerCursor, from, spaceToEnd);
    std::memcpy (to, from + spaceToEnd, size - spaceToEnd);
  }
  else
  {
    std::memcpy (to + writerCursor, from, size);
  }
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  uint8_t* to, size_t size, ConsumerState &consumer)
{
  /*
   * Data availability check must be checked before calling this method
   */
  size_t readerCursor = consumer.cursor ();

  const uint8_t* from = consumer.queue_ptr ();

  if (readerCursor + size > m_maxSize)
  {
    const size_t spaceToEnd = m_maxSize - readerCursor;

    std::memcpy (to, from + readerCursor, spaceToEnd);
    std::memcpy (to + spaceToEnd, from, size - spaceToEnd);
  }
  else
  {
    std::memcpy (to, from + readerCursor, size);
  }

  return size;
}

#if TODO_BATCH_CONSUME
template <typename Allocator, uint16_t MaxNoDropConsumers>
template <typename Buffer>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  const uint8_t *from,
  Buffer        &to,
  size_t         size,
  ConsumerType  &consumer)
{
  ASSERT (size <= to.capacity (),
          "Message size larger than capacity buffer capacity");

  size_t index = MODULUS (consumer.consumed (), m_capacity);
  /*
   * Copy the header from the buffer
   */
  size_t spaceToEnd = m_capacity - index;

  if (spaceToEnd >= size)
  {
    to.push (from + index, size);
  }
  else
  {
    to.push (from + index, spaceToEnd);
    to.push (from, size - spaceToEnd);
  }
  /*
   * Check the data copied wasn't overwritten during the copy
   *
   * This is only relevant if the consumer is configured to allow message drops
   *
   * TODO: is message drops required?
   */
  if (SPMC_EXPECT_FALSE (consumer.message_drops_allowed ()) &&
      SPMC_EXPECT_FALSE ((m_claimed - consumer.consumed ()) > m_capacity))
  {
    /*
     * If a the consumer is configured to allow message drops and the producer
     * has wrapped the buffer and begun overwritting the data just copied,
     * reset the consumer to point to the most recent data.
     */
    consumer.consumed (m_committed.load (std::memory_order_relaxed));

    return false;
  }

  consumer.add (size);

  return true;
}
#endif

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::consumer_checks (
  ConsumerState &consumer)
{
  /*
   * The header and data must be popped from the queue in a single call to
   * pop () so that the committed / claimed indexes represent a state valid
   * for both header and data.
   *
   * On the first attempt to consume data, register the consumer with the
   * producer.
   */
  if (!consumer.registered ())
  {
    register_consumer (consumer);
  }
}

} // namespace detail
} // namespace spmc {
