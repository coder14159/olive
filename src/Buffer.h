#ifndef OLIVE_BUFFER_H
#define OLIVE_BUFFER_H

#include "detail/SharedMemory.h"
#include "detail/Utils.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace olive {

/*
 * A fast minimal circular buffer for caching prefetched serialised data from
 * the main data buffer.
 *
 * Data pushed into the buffer is expected to be of a size equal to or smaller
 * than the Buffer size.
 *
 * This buffer is limited for use with POD types.
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
   * Support creation of the Buffer using named shared memory
   */
  explicit Buffer (size_t size, const Allocator &allocator);
  /*
   * Deallocates the buffer if allocated in local process memory only
   */
  ~Buffer ();
  /*
   * Return true if cache is enabled, ie capacity > 0
   */
  bool enabled () const;

  /*
   * Return the maximum size
   */
  size_t capacity () const;
  /*
   * Reset the capacity of the buffer.
   *
   * Non-standard behaviour: all internal data is deleted on resize
   */
  void capacity (size_t size);

  /*
   * Return true if the buffer contains no data
   */
  bool empty () const;

  /*
   * Clear the contents of Buffer
   */
  void clear ();

  /*
   * Return current size of the buffer
   */
  size_t size () const;

  /*
   * Push a trivially copyable POD object onto the back of the buffer if space
   * is available
   */
  template<class T>
  bool push (const T &data);

  /*
   * Push serialised data onto the back of the buffer if space is available.
   *
   * Fails if data size is larger than Buffer capacity
   */
  bool push (const std::vector<uint8_t> &data);

  /*
   * Push serialised data onto the back of the buffer if space is available.
   * Fails if data size is larger than Buffer capacity
   */
  bool push (const uint8_t* data, size_t size);

  /*
   * Push serialised data from a queue onto the back of the buffer if space is
   * available.
   */
  bool push (SharedMemory::SPSCQueue &queue);

  /*
   * Push data available in the spsc_queue into the Buffer if space is available.
   */
  bool push (boost::lockfree::spsc_queue<uint8_t> &queue);

  /*
   * Pop a POD object off the front of the buffer into data variable
   */
  template<class T>
  bool pop (T& data);

  /*
   * Pop size bytes of data off the front of the buffer and copy into the data
   * variable
   */
  template<typename BufferType>
  bool pop (BufferType &data, size_t size);
  /*
   * Return unrestricted access to the internal buffer
   * Primarily for debugging
   */
  uint8_t *data ();

  /*
   * Print internal state for debugging
   */
  void print_debug ();

private:
  /*
   * Pop size bytes of data off the front of the buffer and copy to the memory
   * which data points to.
   *
   * Assumes data points to memory equal to or larger than size bytes.
   */
  bool pop (uint8_t* data, size_t size);

  /*
   * Return a pointer to the start of the circular buffer data
   */
  uint8_t *begin ();

  /*
   * Push data from a boost::spsc_queue type onto the internal queue
   */
  template<class T>
  bool push_from_spsc_queue (T &queue, size_t size);

private:

  size_t m_capacity = 0; // capacity of Buffer

  size_t m_size  = 0; // current container size
  size_t m_front = 0; // the first value
  size_t m_back  = 1; // one element past the last value

  /*
   * Pointer to the data buffer
   */
  typename Allocator::pointer m_buffer = nullptr;
};

} // namespace olive

#include "Buffer.inl"

#endif // OLIVE_BUFFER_H