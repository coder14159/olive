namespace olive {

template <typename Allocator>
SPSCSink<Allocator>::SPSCSink (const std::string &memoryName,
                               size_t prefetchSize)
{
  namespace bi = boost::interprocess;

  // open an existing shared memory segment
  m_memory = bi::managed_shared_memory (bi::open_only, memoryName.c_str());

  // swap in a valid segment manager
  m_allocator =
   std::make_unique<SharedMemory::Allocator> (m_memory.get_segment_manager ());

  // increment the counter indicating when the client is ready
  std::string readyCounterName = memoryName + ":client:ready";
  auto readyCounter =
    m_memory.find<SharedMemory::Counter> (readyCounterName.c_str ()).first;

  CHECK_SS (readyCounter != nullptr,
    "client ready counter not available: " << readyCounterName);

  // create a queue for streaming data
  std::string queueCounterName = memoryName + ":queue:counter";
  auto queueCounter =
    m_memory.find_or_construct<SharedMemory::Counter> (queueCounterName.c_str ())();

  CHECK_SS (queueCounter != nullptr,
    "queue counter initialisation failed: " << queueCounterName);

  int index = ++(*queueCounter);

  auto queueName = memoryName + ":source:" + std::to_string (index);

  m_queuePtr = m_memory.find_or_construct<SharedMemory::SPSCQueue> (
                                        queueName.c_str())(1, *m_allocator);

  CHECK_SS (m_queuePtr != nullptr,
    "Failed to create shared memory queue: " << queueName);

  BOOST_LOG_TRIVIAL (info) << "SPSCSink constructed " << queueName;

  if (prefetchSize > 0)
  {
    CHECK_SS (prefetchSize > sizeof (Header),
        "The prefetch cache must be larger than message size header size "
        "(cache capacity: "<< m_cache.capacity ()
        << " header size: " << sizeof (Header));

    m_cache.capacity (prefetchSize);
  }

  ++(*readyCounter);
}

template <typename Allocator>
SPSCSink<Allocator>::~SPSCSink ()
{
  stop ();
}

template <typename Allocator>
void SPSCSink<Allocator>::stop ()
{
  if (!m_stop)
  {
    m_stop = true;
  }
}

template <typename Allocator>
bool SPSCSink<Allocator>::next (Header &header, std::vector<uint8_t> &data)
{
  while (!m_stop)
  {
    if (!m_cache.enabled ())
    {
      if (!pop<Header> (header))
      {
        continue;
      }

      if (SPMC_EXPECT_TRUE (header.type != WARMUP_MESSAGE_TYPE &&
                            header.size > 0))
      {
        data.resize (header.size);

        while (!m_stop)
        {
          if (SPMC_EXPECT_TRUE (pop (data.data (), header.size)))
          {
            return true;
          }
        }
      }
    }
    else
    {
      return pop_from_cache (header, data);
    }
  }

  return false;
}

template <typename Allocator>
template <typename POD>
bool SPSCSink<Allocator>::pop (POD &pod)
{
  return pop (reinterpret_cast<uint8_t*> (&pod), sizeof (POD));
}

template <typename Allocator>
bool SPSCSink<Allocator>::pop (uint8_t *to, size_t size)
{
  auto &queue = *m_queuePtr;

  auto available = queue.read_available ();

  if (available < size)
  {
    return false;
  }

  size_t popped_size = queue.pop (to, size);

  assert (popped_size == size);

  return popped_size;
}

template <typename Allocator>
template<class Header, class Data>
bool SPSCSink<Allocator>::pop_from_cache (Header &header, Data &data)
{
  auto &queue = *m_queuePtr;

  size_t available = queue.read_available ();

  if (available == 0)
  {
    return false;
  }
  /*
   * Append as much available data as possible to the cache
   */
  if (m_cache.size () <= sizeof (Header))
  {
    prefetch_to_cache ();
  }

  if (m_cache.pop (header) && header.type != WARMUP_MESSAGE_TYPE)
  {
    if (m_cache.capacity () < header.size)
    {
      /*
       * If a message received is too large to fit in the cache, drain the
       * cache and disable local caching.
       */
      BOOST_LOG_TRIVIAL (warning)
        << "Disable the prefetch cache (" << m_cache.capacity () << " bytes), "
        << "message size is too large (" << header.size << " bytes).";

      m_cache.pop (data, m_cache.size ());

      std::vector<uint8_t> tmp (header.size - data.size ());

      assert (pop (tmp.data (), header.size - data.size ()));

      data.insert (data.end (), tmp.begin (), tmp.end ());

      m_cache.capacity (0);

      return true;
    }

    /*
     * Make sure the payload is received, waiting if necessary
     *
     * TODO: exit this loop if the producer exits before the payload is sent
     */
    while (m_cache.size () < header.size)
    {
      prefetch_to_cache ();
    }

    return m_cache.pop (data, header.size);
  }

  return false;
}

template <typename Allocator>
bool SPSCSink<Allocator>::prefetch_to_cache ()
{
  auto &queue = *m_queuePtr;

  size_t available = queue.read_available ();

  if (available == 0)
  {
    return false;
  }
  /*
   * Push the maximum available data into the prefetch cache buffer
   */
  m_cache.push (queue);

  return true;
}

} // namespace olive {