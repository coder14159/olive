#ifndef OLIVE_SPMC_QUEUE_H
#define OLIVE_SPMC_QUEUE_H

#include "detail/SharedMemory.h"
#include "detail/SPMCQueue.h"

#include <string>
#include <vector>

namespace olive {

/*
 * Single producer / multiple consumer queue which wraps the functionality of
 * the detail::SPMCQueue and adds some additional functionality whiich is local
 * to the client consumer.
 *
 * The producers and consumers can be separate threads or processes.
 */
template <class Allocator,
          uint8_t MaxNoDropConsumers = MAX_NO_DROP_CONSUMERS_DEFAULT>
class SPMCQueue
{
  using QueueType = detail::SPMCQueue<Allocator, MaxNoDropConsumers>;

public:
  /*
   * Construct an SPMCQueue for use by a single producer and multiple consumer
   * threads in a single process.
   */
  SPMCQueue (size_t capacity);

  /*
   * Creates named shared memory and then constructs an SPMCQueue (or opens an
   * existing queue if available) for inter-process communication.
   */
  SPMCQueue (const std::string &memoryName,
             const std::string &queueName,
             size_t             capacity);

  /*
   * Open an existing shared memory SPMCQueue for use by a consumer in
   * inter-process communication.
   *
   * Does not create shared memory or shared objects.
   */
  SPMCQueue (const std::string &memoryName,
             const std::string &queueName);

  /*
   * Register a consumer thread or process
   */
  void register_consumer (detail::ConsumerState &consumer);
  /*
   * Inform producer that a consumer is stopping.
   *
   * Called from the consumer context.
   */
  void unregister_consumer (detail::ConsumerState &consumer);

  /*
   * Return the capacity of the queue in bytes
   */
  size_t capacity () const;

  /*
   * Return true if queue is empty
   */
  bool empty (detail::ConsumerState &consumer) const;

  /*
   * Return the size of data currently available in the queue
   */
  uint64_t read_available (detail::ConsumerState &consumer) const;

  /*
   * Return the minimum size of queue data which is writable taking into account
   * progress of all the consumers
   */
  size_t write_available () const;

  /*
   * Push data of type POD into the queue.
   *
   * Useful if the header type has no associated data. An example would be when
   * sending warmup messages.
   */
  template <class Data>
  bool push (const Data &data);

  /*
   * Push pod type data into the queue, always succeeds unless there are slow
   * consumers configured to be non-dropping.
   *
   * The queue should be larger than the data size + header size.
   */
  template <class Header, class Data>
  bool push (const Header &header, const Data &data);

  /*
   * Push data into the queue. Always succeeds unless there are slow consumers
   * configured to be non-dropping.
   *
   * The queue should be greater than or equal to data size + header size.
   */
  template <class Header>
  bool push (const Header &header, const std::vector<uint8_t> &data);

  /*
   * Pop header and data from the queue
   *
   * The BufferType should have the methods resize () and data ()
   */
  template <class BufferType>
  bool pop (Header &header, BufferType &data, detail::ConsumerState &consumer);

  /*
   * Pop a POD type from the queue
   */
  template <class POD>
  bool pop (POD &pod, detail::ConsumerState &consumer);

private:
  /*
   * Memory shared between processes
   */
  boost::interprocess::managed_shared_memory m_memory;

  typedef typename std::conditional<
          std::is_same<std::allocator<uint8_t>, Allocator>::value,
          std::unique_ptr<QueueType>,
          QueueType*>::type QueuePtr;
  /*
   * The shared queue
   */
  alignas (CACHE_LINE_SIZE)
  QueuePtr m_queue;
};

} // namespace olive {

#include "SPMCQueue.inl"

#endif // OLIVE_SPMC_QUEUE_H
