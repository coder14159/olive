#include <type_traits>

namespace spmc {

namespace bi = boost::interprocess;

template <typename Allocator>
SPSCStream<Allocator>::SPSCStream (const std::string &memoryName)
{
	// open an existing shared memory segment
  m_memory = bi::managed_shared_memory (bi::open_only, memoryName.c_str());

  // swap in a valid segment manager

  m_allocator =
   std::make_unique<SharedMemory::Allocator> (m_memory.get_segment_manager ());

  // increment the counter indicating when the client is ready
  std::string readyCounterName = memoryName + ":client:ready";
  auto readyCounter =
    m_memory.find<SharedMemory::Counter> (readyCounterName.c_str ()).first;

  ASSERT_SS (readyCounter != nullptr,
    "client ready counter initialisation failed: " << readyCounterName);

  // create a queue for streaming data
  std::string queueCounterName = memoryName + ":queue:counter";
  auto queueCounter =
    m_memory.find_or_construct<SharedMemory::Counter> (queueCounterName.c_str ())();

  ASSERT_SS (queueCounter != nullptr,
    "queue counter initialisation failed: " << queueCounterName);

  int index = ++(*queueCounter);

  auto queueName = memoryName + ":sink:" + std::to_string (index);

  m_queuePtr = m_memory.find_or_construct<SharedMemory::SPSCQueue> (
                                        queueName.c_str())(1, *m_allocator);

  ASSERT_SS (m_queuePtr != nullptr,
    "Failed to create shared memory queue: " << queueName);

  BOOST_LOG_TRIVIAL(info) << "SPSCStream constructed " << queueName;

  ++(*readyCounter);
}

template <typename Allocator>
SPSCStream<Allocator>::~SPSCStream ()
{
  stop ();
}

template <typename Allocator>
void SPSCStream<Allocator>::stop ()
{
  if (!m_stop)
  {
    m_stop = true;
  }
}

template <typename Allocator>
bool SPSCStream<Allocator>::next (Header &header, std::vector<uint8_t> &data)
{
  while (!m_stop.load (std::memory_order_relaxed))
  {
   	if (!pop<Header> (header))
    {
      continue;
    }

    if (SPMC_EXPECT_TRUE (header.type != WARMUP_MESSAGE_TYPE && header.size > 0))
    {
      data.resize (header.size);

      while (!m_stop.load (std::memory_order_relaxed))
      {
        if (SPMC_EXPECT_TRUE (pop (data.data (), header.size) > 0))
        {
          return true;
        }
      }
    }
  }

  return false;
}

template <typename Allocator>
template<typename POD>
bool SPSCStream<Allocator>::pop (POD &pod)
{
  return pop (reinterpret_cast<uint8_t*> (&pod), sizeof (POD));
}

template <typename Allocator>
bool SPSCStream<Allocator>::pop (uint8_t *data, size_t size)
{
  /*
   * The queue potentially in shared memory
   */
  auto &queue = *m_queuePtr;

  auto available = queue.read_available ();

  if (available < size)
  {
    return false;
  }

  size_t popped_size = queue.pop (data, size);

  assert (popped_size == size);

  return popped_size;
}

} // namespace spmc {