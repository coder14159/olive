#include <type_traits>

#include <boost/log/trivial.hpp>

namespace spmc {

template <class Allocator>
Buffer<Allocator>::Buffer (size_t capacity)
: m_capacity (capacity)
, m_buffer (Allocator::allocate (m_capacity))
{ }

template <class Allocator>
Buffer<Allocator>::Buffer (size_t capacity, const Allocator &allocator)
: Allocator  (allocator)
, m_capacity (capacity)
, m_buffer (Allocator::allocate (m_capacity))
{ }

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
  if (capacity == m_capacity)
  {
    return;
  }

  if (capacity > m_size)
  {
    auto newBuffer = Allocator::allocate (capacity);
    /*
     * Copy over all existing data on resize
     */
    std::memcpy (newBuffer, m_buffer, m_size);

    Allocator::deallocate (m_buffer, m_capacity);

    m_buffer = newBuffer;

    m_capacity = capacity;
  }
  else
  {
    BOOST_LOG_TRIVIAL (warning) << "Resize Buffer to capacity smaller stored "
                                  << "data clears all internal data";

    clear ();

    auto newBuffer = Allocator::allocate (capacity);

    Allocator::deallocate (m_buffer, m_capacity);

    m_buffer = newBuffer;

    m_capacity = capacity;
  }
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
    std::memcpy (buffer + m_back - 1, data, size);
  }
  else
  {
    // input data wraps the buffer
    size_t spaceToEnd = m_capacity - m_back + 1;

    std::memcpy (buffer + m_back - 1, data, spaceToEnd);

    std::memcpy (buffer, data + spaceToEnd, size - spaceToEnd);
  }

  /*
   * Check: might be faster to avoid the % operation in the calculation below
   */
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
template<typename BufferType>
bool Buffer<Allocator>::pop (BufferType &data, size_t size)
{
  data.resize (size);

  return pop (data.data (), size);
}

template <class Allocator>
bool Buffer<Allocator>::pop (uint8_t* data, size_t size)
{
  if (m_capacity < size)
  {
    size_t newCapacity = (size*2);

    BOOST_LOG_TRIVIAL (info) << "Cache capacity is too small, "
      << "increasing it to the size from " << m_capacity << " bytes "
      << "to " << newCapacity << " bytes";
    /*
     * Data is preserved when increasing Buffer size
     */
    capacity (newCapacity);
  }
  /*
   * Requesting a zero size copy does not fail
   */
  if (size == 0)
  {
    return true;
  }

  /*
   * Ensure "&*" is used to access the (potentially) shared memory pointer so
   * the boost interprocess offset_ptr is able to return the appropriate point
   * in shared memory via operator* () call.
   */
  auto buffer = &*m_buffer;

  size_t spaceToEnd = m_capacity - m_front;

  if (spaceToEnd >= size)
  {
    std::memcpy (data, buffer + m_front, size);
  }
  else
  {
    std::memcpy (data, buffer + m_front, spaceToEnd);

    std::memcpy (data + spaceToEnd, buffer, size - spaceToEnd);
  }

  m_front = (m_front + size) % m_capacity;
  m_size -= size;

  return true;
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