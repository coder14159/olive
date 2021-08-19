namespace spmc {

namespace bi = ::boost::interprocess;

using namespace ::std::literals::chrono_literals;

template <typename Allocator>
SPSCSink<Allocator>::SPSCSink (const std::string &memoryName,
                    const std::string &objectName,
                    size_t             queueSize)

: m_name (objectName)
, m_memory (bi::managed_shared_memory (bi::open_only, memoryName.c_str()))
, m_allocator (m_memory.get_segment_manager ())
, m_queue (m_memory.find_or_construct<SharedMemory::SPSCQueue>
                                  (objectName.c_str())(queueSize, m_allocator))
, m_queueRef (*m_queue)
{
  // TODO clients could create queue and notify sink/server
  // TODO maybe support multiple queues in a single sink

  // construct an object within the shared memory

  CHECK_SS (m_queue != nullptr,
            "shared memory object initialisation failed: " << objectName);
}

template <typename Allocator>
void SPSCSink<Allocator>::stop ()
{
  m_stop = true;
}

template <typename Allocator>
void SPSCSink<Allocator>::next (const std::vector<uint8_t> &data)
{
  Header header;
  header.size   = data.size ();
  header.seqNum = ++m_sequenceNumber;
  /*
   * Push the data packet onto the shared queue if there is available space
   * for both header and data
   */
  size_t size = sizeof (Header) + header.size;

  while (!m_stop && m_queueRef.write_available () < size)
  { }

  if (m_stop)
  {
    return;
  }

  m_buffer.resize (size);
  /*
   * Set timestamp when queue space is available so that only internal queue
   * latency is measured
   *
   * TODO: Add a variadic push function for spscqueue
   */
  header.timestamp = nanoseconds_since_epoch (Clock::now ());

  std::memcpy (m_buffer.data (),
                reinterpret_cast<uint8_t*> (&header), sizeof (Header));
  std::memcpy (m_buffer.data () + sizeof (Header),
                data.data (), data.size ());

  m_queueRef.push (m_buffer.data (), m_buffer.size ());
}

template <typename Allocator>
void SPSCSink<Allocator>::next_keep_warm ()
{
  m_queueRef.push (reinterpret_cast <uint8_t*> (&m_warmupHdr), sizeof (Header));
}

} // spmc