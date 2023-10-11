#include "Assert.h"
#include "Logger.h"
#include "SPSCSources.h"
#include "detail/SharedMemory.h"

#include <atomic>

namespace olive {

namespace bi = boost::interprocess;

// TODO: Further work required...

SPSCSources::SPSCSources (const std::string &memoryName, size_t capacity)
: m_name (memoryName),
  m_memory (bi::managed_shared_memory (bi::create_only, memoryName.c_str())),
  m_allocator (m_memory.get_segment_manager ())
{
  BOOST_LOG_TRIVIAL (info) << "created shared memory: " << memoryName;

  auto requestQueueName = memoryName + ":requests";

  size_t queueSize = 1024;

  // construct the request queue within the shared memory
  m_requests = m_memory.construct<SharedMemory::SPSCQueue>
                                    (requestQueueName.c_str())
                                    (queueSize, m_allocator);

  CHECK_SS (m_queue != nullptr,
            "shared memory object initialisation failed: " << objectName);

  BOOST_LOG_TRIVIAL (info) << "constructed " << objectName;

#if __TODO__
  m_thread = std::thread ([this] () {

    auto requests = *m_requests;

    while (!m_stop)
    {
      if (requests.empty ())
      {
        std::this_thread::sleep_for (Seconds (1));
      }
      else
      {

      }
    }

  });
#endif // __TODO__
}

SPSCSources::~SPSCSources ()
{
  stop ();
}


void SPSCSources::stop ()
{
  m_stop = true;

  m_thread.join ();
}

void SPSCSources::next (const std::vector<uint8_t> &data)
{
  bool success = false;

  while (!success && !m_stop)
  {
    ++m_sequenceNumber;

    Header header;
    header.size      = data.size ();
    header.seqNum    = m_sequenceNumber;
    header.timestamp = Time::now ().serialise ();

    if (send (reinterpret_cast <uint8_t*> (&header), sizeof (Header)))
    {
      send (data.data (), data.size ());
      success = true;
    }
  }
}

bool SPSCSources::send (const uint8_t *data, size_t size)
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

} // OLIVE_SPSC_SOURCES_H
