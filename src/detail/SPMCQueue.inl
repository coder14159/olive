#include "GetSize.h"
#include "Utils.h"
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>

namespace spmc {
namespace detail {

template <typename Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_backPressure (capacity)
, m_maxSize (m_backPressure.max_size ())
, m_capacity (capacity)
, m_buffer (Allocator::allocate (m_maxSize))
, m_bufferProducer (buffer ())
{
  ASSERT (m_capacity > 0, "Invalid capacity");
  ASSERT (m_buffer != nullptr, "Invalid buffer");

  std::fill (m_bufferProducer, m_bufferProducer + m_capacity, 0);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  size_t capacity, const Allocator &allocator)
: Allocator  (allocator)
, m_backPressure (capacity)
, m_maxSize (m_backPressure.max_size ())
, m_capacity (capacity)
, m_buffer (Allocator::allocate (m_maxSize))
, m_bufferProducer (buffer ())
{
  ASSERT (m_capacity > 0, "Invalid capacity");
  ASSERT (m_buffer != nullptr, "Invalid buffer");

  std::fill (m_bufferProducer, m_bufferProducer + m_capacity, 0);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::~SPMCQueue ()
{
  /*
   * This destructor is only invoked by the single process multi-threaded queue
   *
   * The interprocess queue is deallocated when the named shared memory is
   * removed.
   */
  Allocator::deallocate (m_buffer, m_maxSize);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::register_producer ()
{
  BOOST_LOG_TRIVIAL (info) << "Register producer";

  m_backPressure.register_producer ();
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
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
  if (consumer.index () == Index::UnInitialised)
  {
    m_backPressure.register_consumer (consumer);
  }
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  const ConsumerState &consumer)
{
  m_backPressure.unregister_consumer (consumer);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
uint8_t *SPMCQueue<Allocator, MaxNoDropConsumers>::buffer () const
{
  return reinterpret_cast<uint8_t*> (&*m_buffer);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::capacity () const
{
  return m_capacity;
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::read_available (
  const ConsumerState &consumer) const
{
  return m_backPressure.read_available (consumer);
}


template <typename Allocator, uint8_t MaxNoDropConsumers>
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

template <typename Allocator, uint8_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::push_variadic_item (
  const std::vector<uint8_t> &data,
  size_t offset)
{
  return push (data.data (), data.size (), AcquireRelease::No, offset);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
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

template <typename Allocator, uint8_t MaxNoDropConsumers>
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

template <typename Allocator, uint8_t MaxNoDropConsumers>
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
   * Copy data to the shared buffer.
   */
  copy_to_queue (data, m_bufferProducer, size, offset);
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

template <typename Allocator, uint8_t MaxNoDropConsumers>
template <typename POD>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop_test (
  POD &pod, ConsumerState &consumer)
{
  return pop_test (reinterpret_cast<uint8_t*> (&pod), sizeof (POD), consumer);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop_test (
  uint8_t* to, size_t size, ConsumerState &consumer)
{
  /*
   * Copy data from the queue
   */
  size_t copied = copy_from_queue (to, size, consumer);

  consumer.cursor (m_backPressure.advance_cursor (consumer.cursor (), copied));

  return (size == copied);
}


template <typename Allocator, uint8_t MaxNoDropConsumers>
template <typename POD>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
    POD &pod,
    ConsumerState &consumer)
{
  return pop (reinterpret_cast<uint8_t*> (&pod), sizeof (POD), consumer);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
    uint8_t* to,
    size_t   size,
    ConsumerState &consumer)
{
  /*
   * Copy data from the queue if new data is available.
   */
  if (m_backPressure.read_available (consumer) >= size)
  {
    copy_from_queue (to, size, consumer);
    /*
     * Update consumer cursor value and producer back-pressure
     */
    m_backPressure.consumed (consumer, size);

    return true;
  }

  return false;
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
template <typename BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::prefetch_to_cache (
  BufferType   &cache,
  ConsumerState &consumer)
{
  size_t available = m_backPressure.read_available (consumer);

  if (available == 0)
  {
    return false;
  }

  /*
   * Append as much available data as possible to the cache
   */
  size_t size = std::min (cache.capacity () - cache.size (), available);

  if (copy_from_queue (cache, size, consumer))
  {
    m_backPressure.consumed (consumer, size);

    return true;
  }

  return false;
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::copy_to_queue (
  const uint8_t* from, uint8_t* to, size_t size, size_t offset)
{
  // TODO hoist out of copy_to_queue method?
  size_t writerCursor = m_backPressure.committed_cursor ();

  if (offset > 0)
  {
    writerCursor = m_backPressure.advance_cursor (writerCursor, offset);
  }

  if ((writerCursor + size) < m_maxSize)
  {
    std::memcpy (to + writerCursor, from, size);
  }
  else
  {
    /*
     * Copying data wraps over the end of the buffer
     */
    size_t spaceToEnd = m_maxSize - writerCursor;

    std::memcpy (to + writerCursor, from, spaceToEnd);
    std::memcpy (to, from + spaceToEnd, size - spaceToEnd);
  }
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_queue (
  uint8_t* to, size_t size, ConsumerState &consumer)
{
  /*
   * Data availability check must be checked before calling this method
   */
  size_t readerCursor = consumer.cursor ();

  const uint8_t* from = consumer.queue_ptr ();

  if (BOOST_LIKELY (readerCursor + size <= m_maxSize))
  {
    std::memcpy (to, from + readerCursor, size);
  }
  else
  {
    const size_t spaceToEnd = m_maxSize - readerCursor;

    std::memcpy (to, from + readerCursor, spaceToEnd);
    std::memcpy (to + spaceToEnd, from, size - spaceToEnd);
  }

  return size;
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
template <typename BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_queue (
  BufferType &to, size_t size, ConsumerState &consumer)
{
  ASSERT (size <= to.capacity (),
          "Message size larger than capacity buffer capacity");

  size_t readerCursor = consumer.cursor ();

  const uint8_t* from = consumer.queue_ptr ();

  if (readerCursor + size < m_maxSize)
  {
    to.push (from + readerCursor, size);
  }
  else
  {
    const size_t spaceToEnd = m_maxSize - readerCursor;

    to.push (from + readerCursor, spaceToEnd);
    to.push (from, size - spaceToEnd);
  }

  return true;
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
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

} // namespace detail {
} // namespace spmc {
