#include "Utils.h"
#include <boost/log/trivial.hpp>

#include <cmath>

namespace spmc {
namespace detail {

template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (size_t capacity)
: m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
{ }


template <class Allocator, size_t MaxNoDropConsumers>
SPMCQueue<Allocator, MaxNoDropConsumers>::SPMCQueue (
  size_t capacity, const Allocator &allocator)
: Allocator  (allocator)
, m_capacity (capacity)
, m_buffer (Allocator::allocate (capacity))
{ }

template <class Allocator, size_t MaxNoDropConsumers>
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

template <class Allocator, size_t MaxNoDropConsumers>
uint64_t SPMCQueue<Allocator, MaxNoDropConsumers>::committed () const
{
  return m_committed.load (std::memory_order_acquire);
}

template <class Allocator, size_t MaxNoDropConsumers>
size_t SPMCQueue<Allocator, MaxNoDropConsumers>::capacity ()
{
  return m_capacity;
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::unregister_consumer (
  size_t index)
{
  m_backPressure.unregister_consumer (index);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <typename Header, typename Data>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header &header,
  const Data   &data,
  uint8_t *buffer)
{
  static_assert (std::is_trivial<Data>::value,
            "data type passed to SPMCQueue::push () must be trivial");

  return push (header, reinterpret_cast<const uint8_t*>(&data),
               sizeof (Data), buffer);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <typename Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header               &header,
  const std::vector<uint8_t> &data,
  uint8_t *buffer)
{
  if (data.empty ())
  {
    return false;
  }

  return push (header, data.data (), data.size (), buffer);
}

template <class Allocator, size_t MaxNoDropConsumers>
template <typename Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::push (
  const Header  &header,
  const uint8_t *data,
  size_t         size,
  uint8_t       *buffer)
{
  assert (size > 0);

#if USE_ASSERTS
  assert_fn (header.size <= data.size (), [&] () {
             std::cerr << "header.size (%lu) must equal data size "
                       << header.size, data.size ()
                       << std::endl;});

  assert_fn (sizeof (Header) + data.size () <= m_capacity, [&] () {
      std::cerr << "capacity (" << m_capacity << ") " <<
          << "must be greater than sizeof Header (" << sizeof (Header) ") " <<
          << "+ data size (" << data.size ();});
#endif

  /*
   * The producer has just started or restarted
   */
  if (header.seqNum == 1)
  {
    m_committed = 0;
    m_claimed   = 0;
    m_backPressure.reset_consumers ();
  }

  /*
   * Claim a range of the queue to overwrite
   */
  uint64_t claim = m_claimed.load (std::memory_order_relaxed)
                 + sizeof (Header)
                 + size;

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

#if DELETE_DEBUG_CODE
      BOOST_LOG_TRIVIAL(debug) << "Claimed=" << m_claimed
                      << " minConsumed=" << m_backPressure.min_consumed ()
                      << " queueClaim="
                      << m_claimed - m_backPressure.min_consumed ();
#endif
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
  m_claimed.store (claim, std::memory_order_relaxed);

  /*
   * Get the offset pointer from shared memory. It can be somewhat expensive to
   * retrieve but it is possible to cache the pointer locally.
   */
  copy_to_buffer (reinterpret_cast<const uint8_t*> (&header), buffer,
                  sizeof (Header));

  copy_to_buffer (data, buffer, header.size, sizeof (Header));

  size_t committed = sizeof (Header) + size;

  /*
   * Make data available to the consumers.
   *
   * Use a release commit so the data stored cannot be ordered after the commit
   * has been made.
   */
  m_committed.fetch_add (committed, std::memory_order_release);

#if DELETE_DEBUG_CODE
  BOOST_LOG_TRIVIAL(notice) << "Producer m_claimed=" << m_claimed
                            << " m_committed=" << m_committed
                            << " diff=" << (m_claimed-m_committed);
#endif

  return true;
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Header>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  Header               &header,
  std::vector<uint8_t> &data,
  ProducerType         &producer,
  ConsumerType         &consumer,
  const uint8_t        *buffer)
{
  consumer_checks (producer, consumer);

  bool success = false;

  uint64_t &consumed = consumer.consumed ();
  /*
   * Acquire the committed data variable to ensure the data stored in the queue
   * by the producer thread is visible
   */
  auto   committed = m_committed.load (std::memory_order_acquire);

  size_t available = committed - consumed;

  /*
   * Return false if header and payload are not yet available
   */
  if (available < sizeof (Header))
  {
    return false;
  }

#if USE_ASSERTS
  assert_fn  (available > (sizeof (Header), [&available] () {
        "Committed data=%zu <= sizeof (Header)=%lu",
        available, sizeof (Header); };
#endif

  /*
   * Cache a variable for the duration of the call for a small performance
   * improvement, particularly for the in-process consumer as thread-local
   * variables are a hotspot
   */
  bool consumerAllowsMessageDrops = consumer.message_drops_allowed ();

  /*
   * Store a pointer to the shared memory offset location
   */
  // const auto *buffer = &*m_buffer;

  if (copy_from_buffer (buffer, reinterpret_cast<uint8_t*> (&header),
                        sizeof (Header), consumed,
                        consumerAllowsMessageDrops))
  {
    data.resize (header.size);

    if (copy_from_buffer (buffer, data.data (), header.size,
                          consumed, consumerAllowsMessageDrops))
    {
      /*
       * Feed the progress of a no-drop consumer thread back to the producer
       */
      if (consumerAllowsMessageDrops == false)
      {
        m_backPressure.consumed (consumed, producer.index ());
      }

      success = true;
    }
  }

  return success;
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::copy_to_buffer (
  const uint8_t* from, uint8_t* buffer, size_t size, size_t offset)
{
#if USE_ASSERTS
  assert (from != nullptr);
#endif

  /*
   * m_commit is synchronised after this call in the producer thread so relaxed
   * memory ordering is ok here.
   */
  size_t index =
    MODULUS((m_committed.load (std::memory_order_relaxed) + offset), m_capacity);


  if ((index + size) <= m_capacity)
  {
    // input data does not wrap the queue buffer
    std::memcpy (buffer + index, from, size);
  }
  else
  {
    // input data wraps the queue buffer
    size_t spaceToEnd = m_capacity - index;

    std::memcpy (buffer + index, from, spaceToEnd);

    std::memcpy (buffer, from + spaceToEnd, size - spaceToEnd);
  }

}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  const uint8_t* from, uint8_t* to, size_t size,
  uint64_t &consumed, bool messageDropsAllowed)
{
  bool success = true;

  size_t index = MODULUS(consumed, m_capacity);

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
      (m_claimed - consumed) > m_capacity)
  {
    /*
     * If a the consumer is configured to allow message drops and the producer
     * has wrapped the buffer and begun overwriting the data just copied,
     * reset the consumer to point to the most recent data.
     */
    consumed = m_committed;

    success  = false;
  }
  else
  {
    consumed += size;
  }

  return success;
}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  const uint8_t* from, uint8_t* to, size_t size, ConsumerType &consumer)
{
  bool success = true;

  auto& consumed = consumer.consumed();

  size_t index = MODULUS(consumed, m_capacity);

  /*
   * Copy the header from the buffer
   */
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
  if (SPMC_EXPECT_FALSE (consumer.message_drops_allowed ()) &&
     (m_claimed - consumed) > m_capacity)
  {
    /*
     * If a the consumer is configured to allow message drops and the producer
     * has wrapped the buffer and begun overwritting the data just copied,
     * reset the consumer to point to the most recent data.
     */
    consumed = m_committed;

    success  = false;
  }
  else
  {
    consumed += size;
  }

  return success;
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class Buffer>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::copy_from_buffer (
  const uint8_t *from,
  Buffer        &to,
  size_t         size,
  ConsumerType  &consumer)
{
  ASSERT (size <= to.capacity (),
          "Message size larger than capacity buffer capacity");

  bool success = true;

  auto & consumed = consumer.consumed ();

  size_t index = MODULUS(consumed, m_capacity);

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

    success  = false;
  }
  else
  {
    consumed += size;
  }

  return success;
}

template <class Allocator, size_t MaxNoDropConsumers>
void SPMCQueue<Allocator, MaxNoDropConsumers>::initialise_consumer (
  ProducerType &producer,
  ConsumerType &consumer)
{
  BOOST_LOG_TRIVIAL (trace) << "Initialise consumer";

#if 0
  if (m_bufferPtr == nullptr)
  {
    std::cout << "init bufferPtr" << std::endl;
    m_bufferPtr = &*m_buffer;
  }
#endif
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
        throw std::logic_error ("Failed to register a no-drop consumer");
      }

      producer.index (consumerIndex);
    }

    consumer.consumed (0);

#if TRACE_ENABLED
    BOOST_LOG_TRIVIAL(trace) << "consumer.consumed ()="
                             << consumer.consumed ()
                             << " producer.index ()=" << producer.index ();
#endif
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

#if TRACE_ENABLED
      BOOST_LOG_TRIVIAL (trace)
                     "consumer.consumed ()=" << consumer.consumed ()
                      << " m_claimed="           << m_claimed
                      << " m_committed="         << m_committed
                      << " producer.index ()="   << producer.index ()
                      << " m_backPressure.min_consumed ()="
                            << m_backPressure.min_consumed ();
#endif

    }
  }
}

template <class Allocator, size_t MaxNoDropConsumers>
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
  if (producer_restarted (consumer))
  {
    BOOST_LOG_TRIVIAL (info) << "Producer restarted";

    initialise_consumer (producer, consumer);
  }
}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::producer_restarted (
  const ConsumerType &consumer) const
{
  /*
   * The producer has been restarted if the consumed value is larger than
   * the committed index.
   */
  return (consumer.consumed () > m_committed.load (std::memory_order_relaxed));
}

template <class Allocator, size_t MaxNoDropConsumers>
template <class BufferType>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  BufferType   &cache,
  ProducerType &producer,
  ConsumerType &consumer,
  const uint8_t *buffer)
{
  ASSERT (consumer.message_drops_allowed () == false,
          "Consumer message drops facility should not be enabled");

  consumer_checks (producer, consumer);

  bool ret = false;

  auto &consumed  = consumer.consumed ();

  /*
   * Acquire the committed data variable to ensure the data stored in the queue
   * by the producer thread is visible to the consumer.
   */
  auto   committed = m_committed.load (std::memory_order_acquire);
  size_t available = committed - consumed;

  if (available == 0)
  {
    return false;
  }

  /*
   * Append the much available data as possible
   */
  size_t size = std::min (cache.capacity () - cache.size (), available);

  if (copy_from_buffer (buffer, cache, size, consumer))
  {
    m_backPressure.consumed (consumed, producer.index ());

    ret = true;
  }

  return ret;
}

template <class Allocator, size_t MaxNoDropConsumers>
bool SPMCQueue<Allocator, MaxNoDropConsumers>::pop (
  std::vector<uint8_t> &data,
  size_t                size,
  ProducerType         &producer,
  ConsumerType         &consumer,
  const uint8_t        *buffer)
{
  ASSERT (consumer.message_drops_allowed () == false,
          "Consumer message drops facility should not be enabled");

  consumer_checks (producer, consumer);

  bool ret = false;

  auto  &consumed  = consumer.consumed ();
  /*
   * Acquire the committed data variable to ensure the data stored in the queue
   * by the producer thread is visible to the consumer.
   */
  m_committed.load (std::memory_order_acquire);

#if DELETE_ME
  auto   committed = m_committed.load (std::memory_order_acquire);
  size_t available = committed - consumed;
#endif

  /*
   * Append to data
   */
  data.resize (data.size () + size);

  if (copy_from_buffer (buffer, data.data (), size, consumed,
                       consumer.message_drops_allowed ()))
  {
    m_backPressure.consumed (consumed, producer.index ());

    ret = true;
  }

  return ret;
}

} // namespace detail
} // namespace spmc {
