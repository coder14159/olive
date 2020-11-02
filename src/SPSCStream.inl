#include <type_traits>

namespace spmc {

namespace bi = boost::interprocess;

SPSCStream::SPSCStream (const std::string &name)
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
    "Failed to create shared memory queue: " << queueName);

  BOOST_LOG_TRIVIAL(info) << "SPSCStream constructed " << queueName;

  ++(*readyCounter);
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
  while (!m_stop.load (std::memory_order_relaxed))
  {
   	if (!pop<Header> (header))
    {
      continue;
    }

    if (header.type != WARMUP_MESSAGE_TYPE && header.size > 0)
    {
      data.resize (header.size);

      while (!m_stop.load (std::memory_order_relaxed))
      {
        if (pop (data.data (), header.size) > 0)
        {
          return true;
        }
      }
    }
  }

  return false;
}

template<typename POD>
bool SPSCStream::pop (POD &pod)
{
  return pop (reinterpret_cast<uint8_t*> (&pod), sizeof (POD));
}

bool SPSCStream::pop (uint8_t *data, size_t size)
{
  /*
   * The queue potentially in shared memory
   */
  auto &queue = *m_queue;

  auto available = queue.read_available ();

  if (available < size)
  {
    return false;
  }

  size_t popped_size = queue.pop (data, size);

  assert (popped_size == size);

  return popped_size;
}

#if 0
size_t SPSCStream::receive (uint8_t* data, size_t size)
{
  /*
   * The queue potentially in shared memory
   */
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
    return false;
  }

  /*
   * TODO: needs work - drain the cache before disabling
   */
  if (m_cache.capacity () < size)
  {
    BOOST_LOG_TRIVIAL (info)
      << "Disabling local SPSCStream cache as it is too small"
      << " (cache capacity: " << m_cache.capacity ()
      << " message size " << size << ")";

    m_cacheEnabled = false;

    return false;
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
#endif

} // namespace spmc {