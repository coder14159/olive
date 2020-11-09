#include "Utils.h"
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>

namespace spmc {
namespace detail {

template <class Allocator, uint16_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
{
  ASSERT (capacity > sizeof (Header),
          "SPMCQueue capacity must be greater than header size");

  std::fill (m_buffer, m_buffer + m_capacity, 0);
}


template <class Allocator, uint16_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  size_t capacity, const Allocator &allocator)
: Allocator  (allocator)
, m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
{
  ASSERT (capacity > sizeof (Header),
          "SPMCQueue capacity must be greater than header size");

  std::fill (m_buffer, m_buffer + m_capacity, 0);
}

template <class Allocator, uint16_t MaxNoDropConsumers>
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

template <class Allocator, uint16_t MaxNoDropConsumers>
uint8_t *SPMCQueue<Allocator, MaxNoDropConsumers>::buffer () const
{
  return &*m_buffer;
}


template <class Allocator, uint16_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::committed () const
{
  return m_committed.load (std::memory_order_acquire);
}

template <class Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::capacity () const
{
  return m_capacity;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::size () const
{
  return m_committed - m_claimed;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  size_t index)
{
  m_backPressure.unregister_consumer (index);
}

template <class Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::reset_producer ()
{
  m_committed = 0;
  m_claimed   = 0;
  m_backPressure.reset_consumers ();
}

template <class Allocator, uint16_t MaxNoDropConsumers>
template <typename Header, typename Data>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header &header,
  const Data   &data,
  uint8_t      *buffer)
{
  static_assert (std::is_trivial<Data>::value,
            "data type passed to SPMCQueue::push () must be trivial");

  return push (header, reinterpret_cast<const uint8_t*>(&data),
               sizeof (Data), buffer);
}

template <class Allocator, uint16_t MaxNoDropConsumers>
template <typename Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header &header,
  const std::vector<uint8_t> &data,
  uint8_t *buffer)
{
  if (data.empty ())
  {
    return false;
  }

  return push (header, data.data (), data.size (), buffer);
}

template <class Allocator, uint16_t MaxNoDropConsumers>
template <typename Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header  &header,
  const uint8_t *data,
  size_t         size,
  uint8_t       *buffer)
{
  assert (size > 0);

  ASSERT (header.size == size, "Header size must be equal to data size");
  ASSERT (sizeof (Header) + size <= m_capacity,
          "Capacity must be larger than size of Header plus data size");

  /*
   * The producer has just started or restarted
   */
  if (header.seqNum == 1)
  {
    reset_producer ();
  }

  /*
  * Claim a data range of the queue to overwrite with a header and the data.
  *
  * There is only one producer so there should be no ABA issues updating the
  * claimed variable.
  */
  const uint64_t claim = m_claimed.load (std::memory_order_acquire)
                       + sizeof (Header) + size;

  if (m_backPressure.has_non_drop_consumers ())
  {

    uint64_t minConsumed = m_backPressure.min_consumed ();
    /*
     * If no consumers are allowed to drop messages there is no need to moderate
     * the speed of the producer thread.
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
  m_claimed.store (claim, std::memory_order_release);

  /*
   * Copy the header and payload data to the shared buffer.
   */
  copy_to_buffer (reinterpret_cast<const uint8_t*> (&header),
                  buffer, sizeof (Header));

  copy_to_buffer (data, buffer, header.size, sizeof (Header));

  /*
   * Make data available to the consumers.
   *
   * Use a release commit so the data stored cannot be ordered after the commit
   * has been made.
   */
  m_committed.fetch_add (sizeof (Header) + size, std::memory_order_release);

  return true;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
template <typename Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header &header,
  uint8_t *buffer)
{
  /*
   * The producer has just started or restarted
   */
  if (header.seqNum == 1)
  {
    reset_producer ();
  }
  /*
   * Claim a range of the queue to overwrite
   */
  const uint64_t claim = m_claimed.load (std::memory_order_acquire)
                       + sizeof (Header);

  if (m_backPressure.has_non_drop_consumers ())
  {
    uint64_t minConsumed = m_backPressure.min_consumed ();
    /*
     * If no consumers are allowed to drop messages there is no need to moderate
     * the speed of the producer thread.
     *
     * If all consumers which had no drops enabled have stopped then there is no
     * need to feed back consumer progress to the producer.
     */
    if (minConsumed != Consumer::UnInitialised)
    {
      uint64_t queueClaim = claim - minConsumed;

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
  m_claimed.store (claim, std::memory_order_release);

  /*
   * Copy the header to the shared buffer.
   */
  copy_to_buffer (reinterpret_cast<const uint8_t*> (&header),
                  buffer, sizeof (Header));

  size_t committed = sizeof (Header);

  /*
   * Make data available to the consumers.
   *
   * Use a release commit so the data stored cannot be ordered after the commit
   * has been made.
   */
  m_committed.fetch_add (committed, std::memory_order_release);

  return true;
}


template <class Allocator, uint16_t MaxNoDropConsumers>
template <typename Header, typename BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header        &header,
  BufferType    &data,
  ProducerType  &producer,
  ConsumerType  &consumer,
  const uint8_t *buffer)
{
  consumer_checks (producer, consumer);

  uint64_t consumed = consumer.consumed ();
  /*
   * Acquire the committed data variable to ensure the data stored in the queue
   * by the producer thread is visible
   */
  auto committed = m_committed.load (std::memory_order_acquire);

  size_t available = committed - consumed;
  /*
   * Return false if header and payload are not yet available
   */
  if (available < sizeof (Header))
  {
    return false;
  }
  /*
   * Cache a variable for the duration of the call for a small performance
   * improvement, particularly for the in-process consumer as thread-local
   * variables are a hotspot
   *
   * TODO: move value from consumer.message_drops_allowed to member variable
   */
  bool messageDropsAllowed = consumer.message_drops_allowed ();

  auto bytesCopied = copy_from_buffer (buffer,
                        reinterpret_cast<uint8_t*> (&header),
                        sizeof (Header), consumer, messageDropsAllowed);

  if (bytesCopied == 0)
  {
    return false;
  }

  if (header.type == WARMUP_MESSAGE_TYPE)
  {
    consumer.consumed (consumed + bytesCopied);

    if (!messageDropsAllowed)
    {
      m_backPressure.consumed (consumed + bytesCopied, producer.index ());
    }

    return false;
  }

  /*
   * Copy message payload if available
  */
  data.resize (header.size);

  bytesCopied += copy_from_buffer (buffer, data.data (),
                                   header.size, consumer, messageDropsAllowed);
 /*
  * Feed the progress of a no-drop consumer thread back to the producer
  */
  if (!messageDropsAllowed)
  {
    m_backPressure.consumed (consumed + bytesCopied, producer.index ());
  }

  return true;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
template <typename BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  BufferType    &data,
  size_t         size,
  ProducerType  &producer,
  ConsumerType  &consumer,
  const uint8_t *buffer)
{
  consumer_checks (producer, consumer);

  uint64_t consumed = consumer.consumed ();
  /*
   * Acquire the committed data variable to ensure the data stored in the queue
   * by the producer thread is visible
   */
  auto committed = m_committed.load (std::memory_order_acquire);

  size_t available = committed - consumed;
  /*
   * Return false if header and payload are not yet available
   */
  if (available < size)
  {
    return false;
  }
  /*
   * Cache a variable for the duration of the call for a small performance
   * improvement, particularly for the in-process consumer as thread-local
   * variables are a hotspot
   *
   * TODO: move value from consumer.message_drops_allowed to member variable
   */
  bool messageDropsAllowed = consumer.message_drops_allowed ();

  data.resize (size);

  auto bytesCopied = copy_from_buffer (buffer,
                        data.data (), size,
                        consumer, messageDropsAllowed);

  if (bytesCopied == 0)
  {
    return false;
  }
  /*
   * Feed the progress of a no-drop consumer thread back to the producer
   */
  if (!messageDropsAllowed)
  {
    m_backPressure.consumed (consumed + bytesCopied, producer.index ());
  }

  return true;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
template <class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::prefetch_to_cache (
  BufferType   &cache,
  ProducerType &producer,
  ConsumerType &consumer,
  const uint8_t *buffer)
{
  ASSERT (consumer.message_drops_allowed () == false,
          "Consumer message drops facility should not be enabled");

  if (!consumer.initialised ())
  {
    consumer_checks (producer, consumer);
  }

  auto consumed = consumer.consumed ();
  /*
   * Acquire the committed data variable to ensure the data stored in the queue
   * by the producer thread is visible to the consumer.
   */
  auto committed = m_committed.load (std::memory_order_acquire);
  size_t available = committed - consumed;

  if (available == 0)
  {
    return false;
  }

  /*
   * Append as much available data as possible
   */
  size_t size = std::min (cache.capacity () - cache.size (), available);

  if (copy_from_buffer (buffer, cache, size, consumer))
  {
    m_backPressure.consumed (consumed, producer.index ());

    return true;
  }

  return false;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::copy_to_buffer (
  const uint8_t* from, uint8_t* buffer, size_t size, size_t offset)
{
  /*
   * m_commit is synchronised after this call in the producer thread so relaxed
   * memory ordering is ok here.
   */
  size_t index = MODULUS (
    (m_committed.load (std::memory_order_relaxed) + offset), m_capacity);

  size_t spaceToEnd = m_capacity - index - size;

  if (spaceToEnd <= m_capacity)
  {
    // input data does not wrap the queue buffer
    std::memcpy (buffer + index, from, size);

    if (spaceToEnd < CACHE_LINE_SIZE)
    {
      /*
       * If reading near the end of the buffer, read the buffer start to pull a
       * cache line at the start of the buffer into memory.
       */
      auto dummy = buffer[0];
      (void)dummy;
    }
  }
  else
  {
    /*
     * Input data wraps the end of the queue buffer
     */
    size_t spaceToEnd = m_capacity - index;

    std::memcpy (buffer + index, from, spaceToEnd);

    std::memcpy (buffer, from + spaceToEnd, size - spaceToEnd);
  }
}

template <class Allocator, uint16_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  const uint8_t* from, uint8_t* to, size_t size,
  ConsumerType &consumer, bool messageDropsAllowed)
{
  auto consumed = consumer.consumed ();

  size_t index = MODULUS (consumed, m_capacity);

  // copy the header from the buffer
  size_t spaceToEnd = m_capacity - index;

  if (spaceToEnd >= size)
  {
    std::memcpy (to, from + index, size);
  }
  else
  {
    std::memcpy (to, from + index, spaceToEnd);

    std::memcpy (to + spaceToEnd, from, size - spaceToEnd);
  }

  /*
   * Check the data copied wasn't overwritten during the copy
   *
   * This is only relevant if the consumer is configured to allow message drops
   */
  if (SPMC_EXPECT_FALSE (messageDropsAllowed) &&
      SPMC_EXPECT_FALSE ((m_claimed - consumer.consumed ()) > m_capacity))
  {
    /*
     * If a the consumer is configured to allow message drops and the producer
     * has wrapped the buffer and begun overwriting the data just copied,
     * reset the consumer to point to the most recent data.
     */
    consumer.consumed (m_committed);

    return 0;
  }

  consumer.add (size);

  /*
    * If reading near the end of the buffer, read the buffer start to pull a
    * cache line at the start of the buffer into memory.
    */
  if ((spaceToEnd + size) <= CACHE_LINE_SIZE)
  {
    auto dummy = from[0];
    (void)dummy;
  }

  return size;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
template <class Buffer>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  const uint8_t *from,
  Buffer        &to,
  size_t         size,
  ConsumerType  &consumer)
{
  ASSERT (size <= to.capacity (),
          "Message size larger than capacity buffer capacity");

  auto consumed = consumer.consumed ();

  size_t index = MODULUS (consumed, m_capacity);

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
   */
  if (SPMC_EXPECT_FALSE (consumer.message_drops_allowed ()) &&
     (m_claimed - consumed) > m_capacity)
  {
    /*
     * If a the consumer is configured to allow message drops and the producer
     * has wrapped the buffer and begun overwritting the data just copied,
     * reset the consumer to point to the most recent data.
     */
    consumer.consumed (m_committed);

    return false;
  }
  else
  {
    consumer.add (size);
  }

  return true;
}

template <class Allocator, uint16_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::initialise_consumer (
  ProducerType &producer,
  ConsumerType &consumer)
{
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

      ASSERT (consumerIndex != Consumer::UnInitialised,
              "Failed to register a no-drop consumer");

      producer.index (consumerIndex);
    }

    consumer.consumed (0);

    /*
     * If producer wrapped the queue before the producer could be informed of
     * the consumer progress then begin consuming at the most recent data.
     */
    while ((m_committed.load (std::memory_order_acquire) - consumer.consumed ())
              > m_capacity)
    {
      uint64_t committed = m_committed;

      m_backPressure.consumed (committed, producer.index ());

      consumer.consumed (committed);
    }
  }
}

template <class Allocator, uint16_t MaxNoDropConsumers>
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
#if 1 // TODO_READD_RESTART_TEST is this required functionality?
  // if (producer_restarted (consumer))
  if (consumer.consumed () > m_committed.load (std::memory_order_relaxed))
  {
    BOOST_LOG_TRIVIAL (info) << "Producer restarted";

    initialise_consumer (producer, consumer);
  }
#endif
}

template <class Allocator, uint16_t MaxNoDropConsumers>
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
