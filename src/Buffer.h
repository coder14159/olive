#ifndef IPC_BUFFER_H
#define IPC_BUFFER_H

#include "detail/SharedMemory.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace spmc {

/*
 * A fast minimal circular buffer for caching prefetched serialised data from
 * the main data buffer.
 *
 * Data pushed into the buffer is expected to be of a size equal to or smaller
 * than the Buffer size.
 *
 * Should be able to avoid % in calc below
 *
 * m_front = (m_front + size) % m_capacity;
 * m_size -= size;
 *
 */
template <class Allocator>
class Buffer : private Allocator
{
public:
  /*
   * The Buffer is created in local process memory
   */
  explicit Buffer () { }
  /*
   * The Buffer is created in local process memory
   */
  explicit Buffer (size_t size);
  /*
   * The Buffer is created in named shared memory
   */
  explicit Buffer (size_t size, const Allocator &allocator);
  /*
   * Deallocates the buffer if allocated in local process memory only
   */
  ~Buffer ();

  /*
   * Clear the contents
   */
  void clear ();

  /*
   * Reset the capacity if the buffer and reset all internal data
   */
  void capacity (size_t size);

  /*
   * Reset the capacity if the buffer.
   * Non-standard behviour: all internal data is deleted on resize
   */
  void resize (size_t size);

  /*
   * Return the maximum size
   */
  size_t capacity () const;

  /*
   * Return true if the buffer contains no data
   */
  bool empty () const;

  /*
   * Return current size of the buffer
   */
  size_t size () const;

  /*
   * Push a trivially copyable object onto the back of the buffer.
   * Always succeeds.
   */
  template<class T>
  bool push (const T &data);

  /*
   * Push serialised data onto the back of the buffer. Always succeeds.
   */
  bool push (const std::vector<uint8_t> &data);

  /*
   * Push serialised data onto the back of the buffer. Always succeeds.
   */
  bool push (const uint8_t* data, size_t size);

  /*
   * Push serialised data from a queue onto the back of the buffer.
   */
  bool push (SharedMemory::SPSCQueue &queue);

  /*
   * Push data available in the spsc_queue into the Buffer
   */
  bool push (boost::lockfree::spsc_queue<uint8_t> &queue);

  /*
   * Pop a pod object off the front of the buffer
   */
  template<class T>
  bool pop (T& data);

  /*
   * Pop serialised data of known size off the front of the buffer
   */
  bool pop (std::vector<uint8_t> &data, size_t size);

  /*
   * Pop serialised data of known size off the front of the buffer
   */
  bool pop (uint8_t* data, size_t size);

  /*
   * Pop serialised data of known size off the front of the buffer
   */
  void pop (std::vector<uint8_t> &data, uint64_t consumed, size_t size);

  /*
   * Return unrestricted access to the internal buffer - be careful with this
   */
  uint8_t *data ();

  void print_debug ();

private:
  /*
   * Return a pointer to the start of the circular buffer data
   */
  uint8_t *begin ();

  template<class T>
  bool push_from_spsc_queue (T &queue, size_t size);

private:

  size_t m_capacity = 0;

  size_t m_size  = 0; // current container size
  size_t m_front = 0; // the first value
  size_t m_back  = 1; // one element past the last value

  /*
   * The data buffer.
   */
  typename Allocator::pointer m_buffer = nullptr;
};

} // namespace spmc

#include "Buffer.inl"

#endif // IPC_BUFFER_H