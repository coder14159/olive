#ifndef IPC_SPMC_QUEUE_H
#define IPC_SPMC_QUEUE_H

#include "Buffer.h"
#include "detail/SharedMemory.h"
#include "detail/SPMCQueue.h"

#include <string>
#include <vector>

namespace spmc {

/*
 * Single producer / multiple consumer queue
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

  /*
   * Return the size of data currently available in the queue
   */
  uint64_t read_available () const;

  /*
   * Return the size of the data currently stored in the local consumer cache
   */
  void cache_size (size_t size);

  /*
   * Allow a consumer to drop messages if it cannot keep up with the message
   * rate of the producer.
   *
   * Called from a consumer thread context to ensure the producer message rate
   * is not affected buy performance of a given consumer thread or process.
   */
  void allow_message_drops ();

  /*
   * Inform producer that a consumer is stopping.
   *
   * Called from the consumer context.
   */
  void unregister_consumer ();

  /*
   * Push data into the queue, always succeeds unless there are slow consumers
   * configured to be non-dropping.
   *
   * The queue should be larger than the data size + header size.
   */
  template <class Header>
  bool push (const Header &header, const std::vector<uint8_t> &data);

  /*
   * Push pod type data into the queue, always succeeds unless there are slow
   * consumers configured to be non-dropping.
   *
   * The queue should be larger than the data size + header size.
   */
  template <class Header, class Data>
  bool push (const Header &header, const Data &data);

  /*
   * Push just a header to the queue with no data.
   *
   * Useful if the header type has no associated data. An example would be when
   * sending warmup messages.
   */
  template <class Header>
  bool push (const Header &header);

  /*
   * Pop data out of the header and data from the queue
   */
  template <class Header>
  bool pop (Header &header, std::vector<uint8_t> &data);

private:

  typedef typename std::conditional<
          std::is_same<std::allocator<uint8_t>, Allocator>::value,
          std::unique_ptr<QueueType>,
          QueueType*>::type QueuePtr;

  /*
   * The shared queue
   */
  QueuePtr m_queue;

  /*
   * Memory shared between processes
   */
  boost::interprocess::managed_shared_memory m_memory;

  alignas (CACHE_LINE_SIZE)
  bool m_cacheEnabled  = false;

  alignas (CACHE_LINE_SIZE)
  typename QueueType::ConsumerType m_consumer;

  alignas (CACHE_LINE_SIZE)
  typename QueueType::ProducerType m_producer;

  /*
   * Local pointer to data buffer shared between producer and consumers.
   *
   * Dereferencing the boost shared memory offset pointer has a cost. Therefore
   * cache the dereferenced pointer in each client.
   */
  alignas (CACHE_LINE_SIZE)
  uint8_t *m_buffer = { nullptr };
  /*
   * A cache used to store chunks of data taken from the shared queue
   */
  Buffer<std::allocator<uint8_t>> m_cache;

};

} // namespace spmc {

#include "SPMCQueue.inl"

#endif // IPC_SPMC_QUEUE_H
