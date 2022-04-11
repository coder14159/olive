#include "GetSize.h"
#include "Utils.h"
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>

namespace olive {
namespace detail {

template <typename Allocator, uint8_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_backPressure (capacity)
, m_maxSize (m_backPressure.max_size ())
, m_capacity (capacity)
, m_buffer (Allocator::allocate (m_maxSize))
, m_bufferProducer (buffer ())
{
  CHECK (m_capacity > 0, "Invalid capacity");
  CHECK (m_buffer != nullptr, "Invalid buffer");

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
  CHECK (m_capacity > 0, "Invalid capacity");
  CHECK (m_buffer != nullptr, "Invalid buffer");

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
void SPMCQueue<Allocator, MaxNoDropConsumers>::register_consumer (
  ConsumerState &consumer)
{
  /*
   * Register the consumer with a producer
   */
  if (!consumer.registered ())
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
constexpr size_t SPMCQueue<Allocator, MaxNoDropConsumers>::push_variadic_item (
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
  if (m_backPressure.acquire_space (get_size (head, tail...)))
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
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  POD &pod, ConsumerState &consumer)
{
  return pop (reinterpret_cast<uint8_t*> (&pod), sizeof (POD), consumer);
}

template <typename Allocator, uint8_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
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

  if (readerCursor + size < m_maxSize)
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

} // namespace detail {
} // namespace olive {
