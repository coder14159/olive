#include <cassert>
#include <type_traits>

#include <boost/log/trivial.hpp>

namespace spmc {

template <class Allocator>
Buffer<Allocator>::Buffer (size_t capacity)
: m_capacity (capacity)
{
  m_buffer = Allocator::allocate (capacity);
}

template <class Allocator>
Buffer<Allocator>::Buffer (size_t capacity, const Allocator &allocator)
: Allocator  (allocator),
  m_capacity (capacity)
{
  m_buffer = Allocator::allocate (capacity);
}

template <class Allocator>
Buffer<Allocator>::~Buffer ()
{
  Allocator::deallocate (m_buffer, m_capacity);
}

template <class Allocator>
void Buffer<Allocator>::clear ()
{
  m_size  = 0;
  m_front = 0;
  m_back  = 1;
}

template <class Allocator>
void Buffer<Allocator>::capacity (size_t capacity)
{
  auto newBuffer = Allocator::allocate (capacity);

  Allocator::deallocate (m_buffer, m_capacity);

  m_buffer = newBuffer;

  m_capacity = capacity;

  m_size  = 0;
  m_front = 0;
  m_back  = 1;
}

template <class Allocator>
void Buffer<Allocator>::resize (size_t size)
{
  auto newBuffer = Allocator::allocate (size);

  Allocator::deallocate (m_buffer, m_capacity);

  m_buffer = newBuffer;

  m_capacity = size;

  m_size  = 0;
  m_front = 0;
  m_back  = 1;

  m_capacity = size;
}

template <class Allocator>
size_t Buffer<Allocator>::capacity () const
{
  return m_capacity;
}

template <class Allocator>
bool Buffer<Allocator>::empty () const
{
  return (m_size == 0);
}

template <class Allocator>
size_t Buffer<Allocator>::size () const
{
  return m_size;
}

template <class Allocator>
template<typename T>
bool Buffer<Allocator>::push (const T& value)
{
  static_assert (std::is_trivially_copyable<T>::value,
                 "Type is not trivially copyable");

  return push (reinterpret_cast<const uint8_t*> (&value), sizeof (T));
}

template <class Allocator>
bool Buffer<Allocator>::push (const std::vector<uint8_t> &data)
{
  return push (data.data (), data.size ());
}

template <class Allocator>
bool Buffer<Allocator>::push (const uint8_t* data, size_t size)
{
  if (size == 0 || (m_size + size) > m_capacity)
  {
    return false;
  }

  /*
   * Ensure "&*" is used to access the (potentially) shared memory
   * pointer so the boost interprocess offset_ptr is able to return the
   * appropriate point in shared memory via operator* () call
   */
  auto buffer = &*m_buffer;

  if ((m_back + size - 1) <= m_capacity)
  {
    // input data does not wrap the buffer
    std::uninitialized_copy_n (data, size, buffer + m_back - 1);
  }
  else
  {
    // input data wraps the data buffer
    size_t spaceToEnd = m_capacity - m_back + 1;

    std::uninitialized_copy_n (data, spaceToEnd, buffer + m_back - 1);

    std::uninitialized_copy_n (data + spaceToEnd, size - spaceToEnd, buffer);
  }

  m_back  = (m_back + size) % m_capacity;
  m_size += size;

  return true;
}

template <class Allocator>
template<typename T>
bool Buffer<Allocator>::push_from_spsc_queue (T &queue, size_t size)
{
  if (size == 0 || (m_size + size) > m_capacity)
  {
    return false;
  }
  /*
   * Ensure "&*" is used to access the (potentially) shared memory
   * pointer so the boost interprocess offset_ptr is able to return the
   * appropriate point in shared memory via operator* () call
   */
  auto buffer = &*m_buffer;

  if ((m_back + size - 1) <= m_capacity)
  {
    /*
     * Pop from the queue into into the buffer
     */
    queue.pop (buffer + m_back - 1, size);
  }
  else
  {
    /*
     * Pop from the queue into into the buffer
     */
    size_t spaceToEnd = m_capacity - m_back + 1;

    queue.pop (buffer + m_back - 1, spaceToEnd);

    queue.pop (buffer, size - spaceToEnd);
  }

  m_back  = (m_back + size) % m_capacity;
  m_size += size;

  return true;
}

template <class Allocator>
bool Buffer<Allocator>::push (SharedMemory::SPSCQueue &queue)
{
  auto queue_size = queue.read_available ();
  auto size       = std::min (m_capacity - m_size, queue_size);

  return push_from_spsc_queue (queue, size);
}

template <class Allocator>
bool Buffer<Allocator>::push (boost::lockfree::spsc_queue<uint8_t> &queue)
{
  auto queue_size = queue.read_available ();
  auto size       = std::min (m_capacity - m_size, queue_size);

  return push_from_spsc_queue (queue, size);
}

template <class Allocator>
template<class T>
bool Buffer<Allocator>::pop (T& value)
{
  static_assert (std::is_trivially_copyable<T>::value,
                 "Type is not trivially copyable" );

  size_t size = sizeof (T);

  assert (size <= m_capacity);

  if (m_size < size)
  {
    return false;
  }

  return pop (reinterpret_cast<uint8_t*> (&value), size);
}

template <class Allocator>
bool Buffer<Allocator>::pop (std::vector<uint8_t> &data, size_t size)
{
  if (m_size < size)
  {
    return false;
  }

  data.resize (size);

  return pop (data.data (), size);
}

template <class Allocator>
bool Buffer<Allocator>::pop (uint8_t* data, size_t size)
{
  assert (size <= m_capacity);

  if (size > m_size)
  {
    return false;
  }

  size_t spaceToEnd = m_capacity - m_front;

  /*
   * Ensure "&*" is used to access the (potentially) shared memory pointer so
   * the boost interprocess offset_ptr is able to return the appropriate point
   * in shared memory via operator* () call.
   */
  auto buffer = &*m_buffer;

  if (spaceToEnd >= size)
  {
    std::uninitialized_copy_n (buffer + m_front, size, data);
  }
  else
  {
    std::uninitialized_copy_n (buffer + m_front, spaceToEnd, data);

    std::uninitialized_copy_n (buffer, size - spaceToEnd, data + spaceToEnd);
  }

  m_front = (m_front + size) % m_capacity;
  m_size -= size;

  return true;
}

template <class Allocator>
void Buffer<Allocator>::pop (
  std::vector<uint8_t> &data,
  uint64_t consumed,
  size_t   size)
{
  assert (size <= m_capacity);

  data.resize (size);

  size_t index = consumed % m_capacity;

  // copy the header from the buffer
  size_t spaceToEnd = m_capacity - consumed;

  /*
   * Ensure "&*" is used to access the (potentially) shared memory
   * pointer so the boost interprocess offset_ptr is able to return the
   * appropriate point in shared memory via operator* () call
   */
  auto *buffer = &*m_buffer;

  if (spaceToEnd >= size)
  {
    std::uninitialized_copy_n (data.data () + index, size, data.data ());
  }
  else
  {
    std::uninitialized_copy_n (buffer + index, spaceToEnd, data.data ());

    std::uninitialized_copy_n (buffer, size - spaceToEnd,
                               data.data () + spaceToEnd);
  }

  m_size -= size;
}

template <class Allocator>
uint8_t *Buffer<Allocator>::data ()
{
  return &*m_buffer;
}

template <class Allocator>
uint8_t *Buffer<Allocator>::begin ()
{
  return (&*m_buffer + m_front);
}


template <class Allocator>
void Buffer<Allocator>::print_debug ()
{
  BOOST_LOG_TRIVIAL (debug) << "buffer: front=" << m_front
                            << " back=" << m_back
                            << " size=" << m_size
                            << " m_capacity=" << m_capacity;

}

} // namespace spmc