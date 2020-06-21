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

  assert_expr (m_queue != nullptr, [&objectName]() {
               std::cerr << "shared memory object initialisation failed: "
                         << objectName << std::endl; });
}


void SPSCSink::stop ()
{
  m_stop = true;
}

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
  size_t packetSize = sizeof (Header) + header.size;

  long waitCounter = 0;

  while (packetSize > queue.write_available ())
  {
    /*
     * Wait for space to become available
     */
    ++waitCounter;

    if (waitCounter == 100000)
    {
      std::this_thread::sleep_for (1ns);

      if (m_stop)
      {
        return;
      }

      waitCounter = 0;
    }
  }

  queue.push (reinterpret_cast <uint8_t*> (&header), sizeof (Header));

  queue.push (data.data (), header.size);
}

#if 0
bool SPSCSink::send (const uint8_t *data, size_t size)
{
  bool ret = false;

  /*
   * Access the queue in shared memory
   */
  auto &queue = *m_queue;

  while (!m_stop)
  {
    if (queue.write_available () >= size)
    {
      queue.push (data, size);

      ret = true;
      break;
    }
  }

  return ret;
}
#endif
}
