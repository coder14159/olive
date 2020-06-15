#include "Assert.h"
#include "SPSCSink.h"
#include "Time.h"
#include "detail/SharedMemory.h"

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <atomic>

namespace bi = boost::interprocess;

namespace spmc {

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
  bool success = false;
  while (!success && !m_stop)
  {
    ++m_sequenceNumber;

    Header header;
    header.size      = data.size ();
    header.seqNum    = m_sequenceNumber;
    header.timestamp = Clock::now ().time_since_epoch ().count ();

    if (send (reinterpret_cast <uint8_t*> (&header), sizeof (Header)))
    {
      send (data.data (), data.size ());
      success = true;
    }
  }
}

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

}
