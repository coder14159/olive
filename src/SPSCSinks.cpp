#include "Logger.h"
#include "SPSCSink.h"

#include "detail/SharedMemory.h"

#include <atomic>

namespace bi = boost::interprocess;

namespace spmc {

SPSCSinks::SPSCSinks (const std::string &memoryName)
: m_name (objectName),
  m_memory (bi::managed_shared_memory (bi::create_only, memoryName.c_str())),
  m_allocator (m_memory.get_segment_manager ())
{
  ipc::logger ().info () << "created shared memory: " << memoryName;

  auto requestQueueName = memoryName + ":requests";

  size_t queueSize = 1024;

  // construct the request queue within the shared memory
  m_requests = m_memory.construct<SharedMemory::SPSCQueue>
                                    (requestQueueName.c_str())
                                    (queueSize, m_allocator);

  ASSERT_SS (m_queue != nullptr,
             "shared memory object initialisation failed: " << objectName);

  ipc::logger ().info () << "constructed " << objectName;

  m_thread = std::thread ([this] () {

    auto requests = *m_requests;

    while (!m_stop)
    {
      if (requests.empty ())
      {
        sleep_for (milliseconds (100));
      }
      else
      {

      }
    }

  });
}

SPSCSinks::~SPSCSinks ()
{
  stop ();
}


void SPSCSinks::stop ()
{
  m_stop = true;

  m_thread.join ();
}

void SPSCSinks::next (const std::vector<uint8_t> &data)
{
  bool success = false;

  while (!success && !m_stop)
  {
    ++m_sequenceNumber;

    Header header;
    header.size      = data.size ();
    header.seqNum    = m_sequenceNumber;
    header.timestamp = spmc::Time::now ().serialise ();

    if (send (reinterpret_cast <uint8_t*> (&header), sizeof (Header)))
    {
      send (data.data (), data.size ());
      success = true;
    }
  }
}

bool SPSCSinks::send (const uint8_t *data, size_t size)
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
