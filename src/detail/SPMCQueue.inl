#include "GetSize.h"
#include "Utils.h"
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>

namespace spmc {
namespace detail {

template <typename Allocator, uint16_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
, m_bufferProducer (buffer ())
{
  ASSERT (capacity > 0, "Invalid capacity");

  std::fill (m_bufferProducer, m_bufferProducer + m_capacity, 0);
}

// TODO: is this constructor required?
// No need to pass in the allocator as only the type is required,
template <typename Allocator, uint16_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  size_t capacity, const Allocator &allocator)
: Allocator  (allocator)
, m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
, m_bufferProducer (&*m_buffer)
{
  ASSERT (capacity > 0, "Invalid capacity");

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
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::size (
  const ConsumerType &consumer) const
{
  return m_committed - consumer.consumed ();
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::acquire_space (size_t size)
{
  /*
   * Claim a data range of the queue to overwrite with a header and the data.
   *
   * There is only one producer so there should be no ABA issues updating the
   * claimed variable.
   */
  const uint64_t claim = m_claimed + size;

  if (SPMC_EXPECT_TRUE (m_backPressure.has_non_drop_consumers ()))
  {

    uint64_t minConsumed = m_backPressure.min_consumed ();

    /*
     * If all consumers are allowed to drop messages there is no need to
     * moderate the speed of the producer thread.
     *
     * If all consumers which had no drops enabled have stopped then there is no
     * need to feed back consumer progress to the producer.
     */
    if (minConsumed != Consumer::UnInitialised)
    {
      const uint64_t queueClaim = claim - minConsumed;
      /*
       * If "no message drops" has been enabled check if any registered no-drop
       * consumers are far enough behind to prevent the producer from writing to
       * the queue without causing consumer message drops.
       *
       * Claimed variable is only written by this thread.
       */
      if (queueClaim > m_capacity)
      {
        return false;
      }
    }
  }

  /*
   * A queue data range has been successfully claimed so overwriting the range
   * should always be successful.
   */
  m_claimed = claim;

  return true;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::release_space ()
{
  m_committed.store (m_claimed, std::memory_order_release);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::print_debug () const
{
  std::cout << "m_claimed: " << m_claimed
            << " committed: " << m_committed
            << " diff: " << (m_claimed - m_committed)
            << std::endl;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  size_t index)
{
  m_backPressure.unregister_consumer (index);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::reset_producer ()
{
  m_committed = 0;
  m_claimed   = 0;
  m_backPressure.reset_consumers ();
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
  if (SPMC_EXPECT_TRUE (acquire_space (get_size (head, tail...))))
  {
    push_variadic_item (tail..., push_variadic_item (head));

    release_space ();

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
  if (acquire_release == AcquireRelease::Yes && !acquire_space (size))
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
    m_committed.fetch_add (size, std::memory_order_release);
  }

  return size;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
template <typename POD>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
    POD &pod, ProducerType &producer, ConsumerType &consumer)
{
  return pop (reinterpret_cast<uint8_t*> (&pod), sizeof (POD),
              producer, consumer);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
    uint8_t* data,
    size_t size,
    ProducerType &producer,
    ConsumerType &consumer)
{
  const uint64_t consumed = consumer.consumed ();
  /*
   * Check to see if new data is available. Avoid using synchronisation if no
   * new data is available.
   *
   * Return false if data is not yet available in the queue
   */
  if (SPMC_EXPECT_FALSE ((m_committed.load (std::memory_order_relaxed) - consumed) < size))
  {
    return false;
  }
  /*
   * Acquire memory ordering to ensure the data stored in the queue by the
   * producer thread/process is visible after the load call
   */
  m_committed.load (std::memory_order_acquire);
  /*
   * Cache a variable for the duration of the call for a small performance
   * improvement, particularly for the in-process consumer as thread-local
   * variables are a hotspot
   */
  const size_t bytesCopied = copy_from_buffer (data, size, consumer);

  if (SPMC_EXPECT_TRUE ((bytesCopied > 0) && !consumer.message_drops_allowed ()))
  {
    m_backPressure.consumed (consumed + bytesCopied, producer.index ());

    return true;
  }

  return (bytesCopied > 0);
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
template <typename BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::prefetch_to_cache (
  BufferType   &cache,
  ProducerType &producer,
  ConsumerType &consumer)
{
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
    m_backPressure.consumed (consumer.consumed (), producer.index ());

    return true;
  }

  return false;
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::copy_to_buffer (
  const uint8_t* from, uint8_t* buffer, size_t size, size_t offset)
{
  /*
   * m_commit is synchronised after this call in the producer thread so relaxed
   * memory ordering is ok here.
   */
  const size_t index =
    MODULUS ((m_committed.load (std::memory_order_acquire) + offset), m_capacity);

  const size_t spaceToEnd = m_capacity - index;

  if (size <= spaceToEnd)
  {
    // input data does not wrap the queue buffer
    std::memcpy (buffer + index, from, size);
  }
  else
  {
    /*
     * Input data wraps the end of the queue buffer
     *
     * Always copy to the start of the target buffer first to be cache
     * prediction friendly
     */
    std::memcpy (buffer, from + spaceToEnd, size - spaceToEnd);

    std::memcpy (buffer + index, from, spaceToEnd);
  }
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  uint8_t* to, size_t size, ConsumerType &consumer)
{
  size_t index = MODULUS (consumer.consumed (), m_capacity);

  // copy the header from the buffer
  size_t spaceToEnd = m_capacity - index;

  auto *buffer = consumer.queue_ptr ();

  if (SPMC_EXPECT_TRUE (spaceToEnd >= size))
  {
    std::memcpy (to,  buffer + index, size);
  }
  else
  {
    std::memcpy (to, buffer + index, spaceToEnd);

    std::memcpy (to + spaceToEnd, buffer, size - spaceToEnd);
  }
  /*
   * Check the data copied wasn't overwritten during the copy
   *
   * This is only relevant if the consumer is configured to allow message drops
   */
  if (SPMC_EXPECT_FALSE (consumer.message_drops_allowed () &&
                        (m_claimed - consumer.consumed () + size) > m_capacity))
  {
    consumer.consumed (m_committed.load (std::memory_order_relaxed));

    return 0;
  }

  consumer.add (size);

  return size;
}

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

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::initialise_consumer (
  ProducerType &producer,
  ConsumerType &consumer)
{
  /*
   * Store a local pointer to the shared memory data buffer
   */
  consumer.queue_ptr (buffer ());

  if (consumer.message_drops_allowed ())
  {
    /*
     * On startup, begin consuming data from the start of the queue buffer
     * unless the producer has overwritten more than the whole the buffer.
     */
    if (m_committed < m_capacity)
    {
      consumer.consumed (0);
    }
    else
    {
      consumer.consumed (m_committed);
    }
  }
  else
  {
    /*
     * Register a consumer thread
     */
    if (producer.index () == Consumer::UnInitialisedIndex)
    {
      auto consumerIndex = m_backPressure.register_consumer ();

      if (consumerIndex == Consumer::UnInitialised)
      {
        BOOST_LOG_TRIVIAL (warning)
          << "consumerIndex != Consumer::UnInitialised:"
          << " Failed to register a no-drop consumer - too many consumers";
        return;
      }

      producer.index (consumerIndex);
    }

    consumer.consumed (0);

    /*
     * If producer wrapped the queue before the producer could be informed of
     * the consumer progress then begin consuming at the most recent data.
     */
    while ((m_committed - consumer.consumed ()) > m_capacity)
    {
      m_backPressure.consumed (m_committed, producer.index ());

      consumer.consumed (m_committed);
    }
  }
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::consumer_checks (
  ProducerType &producer,
  ConsumerType &consumer)
{
  /*
   * The header and data must be popped from the queue in a single call to
   * pop () so that the committed / claimed indexes represent a state valid
   * for both header and data.
   *
   * On the first attempt to consume data, initialise the producer and consumer
   */
  if (!consumer.initialised ())
  {
    initialise_consumer (producer, consumer);
  }
  /*
   * The producer has been restarted if the consumed value is larger than
   * the committed index.
   */
#if TODO
  // Is this required functionality?
  // It is not currently required as restarting a consumer without deleting the
  // shared memory would preserve the sequence number
  // If we do not use ever increasing produce/consumer index values then it
  // would be required
  if (producer_restarted (consumer))
  {
    BOOST_LOG_TRIVIAL (info) << "Producer restarted";

    initialise_consumer (producer, consumer);
  }
#endif
}

template <typename Allocator, uint16_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::producer_restarted (
  const ConsumerType &consumer) const
{
  /*
   * The producer has been restarted if the consumed value is larger than
   * the committed index.
   */
  return (consumer.consumed () > m_committed.load (std::memory_order_relaxed));
}

} // namespace detail
} // namespace spmc {
