namespace spmc {

namespace bi = ::boost::interprocess;

using namespace ::std::literals::chrono_literals;

SPSCSink::SPSCSink (const std::string &memoryName,
                    const std::string &objectName,
                    size_t             queueSize)

: m_name (objectName),
  m_memory (bi::managed_shared_memory (bi::open_only, memoryName.c_str())),
  m_allocator (m_memory.get_segment_manager ())
{
  // TODO clients could create queue and notify sink/server
  // TODO maybe support multiple queues in a single sink

  // construct an object within the shared memory
  m_queue = m_memory.find_or_construct<SharedMemory::SPSCQueue>
                                  (objectName.c_str())(queueSize, m_allocator);

  ASSERT_SS(m_queue != nullptr,
            "shared memory object initialisation failed: " << objectName);
}


void SPSCSink::stop ()
{
  m_stop = true;
}

inline
void SPSCSink::next (const std::vector<uint8_t> &data)
{
  ++m_sequenceNumber;

  Header header;

  header.size      = data.size ();
  header.seqNum    = m_sequenceNumber;
  header.timestamp = Clock::now ().time_since_epoch ().count ();

  auto &queue = *m_queue;
  /*
   * Push the data packet onto the shared queue if there is available space
   */
  size_t size = sizeof (Header) + header.size;

  while (m_stop.load (std::memory_order_relaxed) == false &&
         queue.write_available () < size)
  { }

  queue.push (reinterpret_cast <uint8_t*> (&header), sizeof (Header));

  queue.push (data.data (), header.size);
}

void SPSCSink::next_keep_warm ()
{
  // TODO store the queue pointer
  auto &queue = *m_queue;

  queue.push (reinterpret_cast <uint8_t*> (&m_warmupHdr), sizeof (Header));
}

}
