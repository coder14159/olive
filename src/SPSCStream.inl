
namespace spmc {

namespace bi = boost::interprocess;

SPSCStream::SPSCStream (const std::string &name, size_t prefetchCache)
{
	// open an existing shared memory segment
  m_memory = bi::managed_shared_memory (bi::open_only, name.c_str());

  // swap in a valid segment manager

  m_allocator =
   std::make_unique<SharedMemory::Allocator> (m_memory.get_segment_manager ());

  // increment the counter indicating when the client is ready
  std::string readyCounterName = name + ":client:ready";
  auto readyCounter =
    m_memory.find<SharedMemory::Counter> (readyCounterName.c_str ()).first;

  ASSERT_SS (readyCounter != nullptr,
    "client ready counter initialisation failed: " << readyCounterName);

  // create a queue for streaming data
  std::string queueCounterName = name + ":queue:counter";
  auto queueCounter =
    m_memory.find_or_construct<SharedMemory::Counter> (queueCounterName.c_str ())();

  ASSERT_SS (queueCounter != nullptr,
    "queue counter initialisation failed: " << queueCounterName);

  auto &counter = *queueCounter;
  int index = ++counter;

  auto queueName = name + ":sink:" + std::to_string (index);

  m_queue = m_memory.find_or_construct<SharedMemory::SPSCQueue> (
                                        queueName.c_str())(1, *m_allocator);

  ASSERT_SS (m_queue != nullptr,
    "Failed to create shared memory queue: " << queueNam);

  BOOST_LOG_TRIVIAL(info) << "SPSCStream constructed " << queueName;

  ++(*readyCounter);

  if (prefetchCache > 0)
  {
    m_cache.capacity (prefetchCache);

    m_cacheEnabled = true;
  }
}

SPSCStream::~SPSCStream ()
{
  stop ();
}

void SPSCStream::stop ()
{
  if (!m_stop)
  {
    m_stop = true;
  }
}


bool SPSCStream::next (Header &header, std::vector<uint8_t> &data)
{
  bool success = false;

  while (!m_stop && !success)
  {
   	auto headerSize =
      receive (reinterpret_cast<uint8_t*> (&header), sizeof (Header));

    if (headerSize > 0)
    {
      data.resize (header.size);

      while (!m_stop)
      {
        auto packetSize = receive (data.data (), header.size);

        if (packetSize > 0)
        {
          success = true;
          break;
        }
      }
    }
  }

  return success;
}

size_t SPSCStream::receive (uint8_t* data, size_t size)
{

  // the queue in shared memory
  auto &queue = *m_queue;


  // TODO need receive policies (eg backoff/yield)
  auto available = queue.read_available ();

  if (available == 0)
  {
    return 0;
  }

  if (!m_cacheEnabled)
  {
    if (available >= size)
    {
      return queue.pop (data, size);
    }
  }

  /* needs work - drain the cache before disabling*/
  if (m_cache.capacity () < size)
  {
    m_cacheEnabled = false;

    return 0;
  }

  /*
   * The cache is enabled
   */
  if (m_cache.size () < size)
  {
    /*
     * Push a batch of data taken from the queue into the cache.
     */
    m_cache.push (queue);
  }

  m_cache.pop (static_cast<uint8_t*> (data), size);

  return size;
}

} // namespace spmc {