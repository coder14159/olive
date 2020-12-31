#ifndef IPC_SPMC_QUEUE_H
#define IPC_SPMC_QUEUE_H

#include "Buffer.h"
#include "detail/SharedMemory.h"
#include "detail/SPMCQueue.h"

#include <string>
#include <vector>

namespace spmc {

/*
 * Single producer / multiple consumer queue which wraps the functionality of
 * the detail::SPMCQueue and adds some additional functionality whiich is local
 * to the client consumer.
 *
 * The producers and consumers can be separate threads or processes.
 */
template <class Allocator,
          size_t MaxNoDropConsumers = MAX_NO_DROP_CONSUMERS_DEFAULT>
class SPMCQueue
{
  using QueueType = detail::SPMCQueue<Allocator, MaxNoDropConsumers>;

public:
  /*
   * Construct an SPMCQueue for multiple threads in a single process.
   */
  SPMCQueue (size_t capacity);

  /*
   * Construct an SPMCQueue for multiple processes.
   *
   * Finds or creates named the shared memory and then finds or creates a queue
   * object in within the shared memory.
   */
  SPMCQueue (const std::string &memoryName,
             const std::string &queueName,
             size_t             capacity);

  /*
   * Find a shared memory SPMCQueue for multiple process access.
   *
   * Does not create shared memory or shared objects.
   */
  SPMCQueue (const std::string &memoryName,
             const std::string &queueName);

  void register_consumer ();
  /*
   * Inform producer that a consumer is stopping.
   *
   * Called from the consumer context.
   */
  void unregister_consumer ();

  /*
   * Return the capacity of the queue in bytes
   */
  size_t capacity () const;

  /*
   * Return true if queue is empty
   */
  bool empty () const;

  /*
   * Allow a consumer to drop messages if it cannot keep up with the message
   * rate of the producer. The default is non-dropping.
   *
   * Called from a consumer thread context to ensure the producer message rate
   * is not affected buy performance of a given consumer thread or process.
   */
  void allow_message_drops ();

  /*
   * Allow the consumer to drop messages if it consumes data too slowly so that
   * it doesn't slow down other non-drop consumers.
   */
  bool message_drops_allowed () const;

  /*
   * Return the size of data currently available in the queue
   */
  uint64_t read_available () const;

  /*
   * Return the size of data currently available in the queue
   */
  uint64_t write_available () const;

  /*
   * Return the capacity of the consumer local data cache
   */
  size_t cache_capacity () const;

  /*
   * Return true if the cache is currently enabled
   */
  bool cache_enabled () const;

  /*
   * Return the current size of the consumer local data cache
   */
  size_t cache_size () const;

  /*
   * Set the capacity of the data cache which is local to each consumer
   */
  void resize_cache (size_t size);

  /*
   * Push a single POD type into the queue.
   *
   * Useful if the header type has no associated data. An example would be when
   * sending warmup messages.
   */
  template <class POD>
  bool push (const POD &pod);

  /*
   * Push pod type data into the queue, always succeeds unless there are slow
   * consumers configured to be non-dropping.
   *
   * The queue should be larger than the data size + header size.
   */
  template <class Header, class Data>
  bool push (const Header &header, const Data &data);

  /*
   * Push data into the queue, always succeeds unless there are slow consumers
   * configured to be non-dropping.
   *
   * The queue should be greater than or equal to data size + pod size.
   */
  template <class Header>
  bool push (const Header &header, const std::vector<uint8_t> &data);

  /*
   * Pop data out of the header and data from the queue
   */
  template <class POD, class BufferType>
  bool pop (POD &pod, BufferType &data);

private:

  /*
   * Pop a chunk of data from the shared queue to a local data cache and return
   * a message header and associated data from the local cache
   */
  template <class Header, class BufferType>
  bool pop_from_cache (Header &header, BufferType &data);

  /*
   * Memory shared between processes
   */
  boost::interprocess::managed_shared_memory m_memory;

  typename QueueType::ConsumerType m_consumer;

  typename QueueType::ProducerType m_producer;

  bool m_cacheEnabled  = false;

  typedef typename std::conditional<
          std::is_same<std::allocator<uint8_t>, Allocator>::value,
          std::unique_ptr<QueueType>,
          QueueType*>::type QueuePtr;
  /*
   * The shared queue
   */
  alignas (CACHE_LINE_SIZE)
  QueuePtr m_queue;
  /*
   * Local pointer to data buffer shared between producer and consumers.
   *
   * Dereferencing the boost shared memory offset pointer has a measurable cost,
   * so cache the dereferenced pointer in each client.
   */
  alignas (CACHE_LINE_SIZE)
  uint8_t *m_buffer  { nullptr };
  /*
   * This data cache can optionally be used to store chunks of data taken from
   * the shared queue. This cache is local to each client.
   */
  Buffer<std::allocator<uint8_t>> m_cache;

};

} // namespace spmc {

#include "SPMCQueue.inl"

#endif // IPC_SPMC_QUEUE_H
