#ifndef OLIVE_SPSC_SOURCES_H
#define OLIVE_SPSC_SOURCES_H

#include "SPSCSource.h"
#include "detail/SharedMemory.h"

#include <atomic>
#include <string>
#include <thread>

namespace olive {

/*
 * Use SPSCSources to manage pushing data into multiple SPSC queues for shared
 * memory interprocess communication.
 *
 * TODO: complete and create tests for this prototype
 */
class SPSCSources
{
public:
  SPSCSources (const std::string &memoryName, size_t capacity);

  ~SPSCSources ();

  /*
   * Stop servicing the internal queues in preparation for shutdown
   */
  void stop ();

  /*
   * Send a data packet to the shared memory queue
   */
  void next (const std::vector<uint8_t> &data);

  /*
   * Return the shared memory name for the queue
   */
  std::string name () const { return m_name; }

private:

  bool send (const uint8_t* data, size_t size);

private:

  std::string m_name;

  /*
   * Client communication channels
   */
  SharedMemory::SPSCQueue *m_requests = { nullptr };
  SharedMemory::SPSCQueue *m_response = { nullptr };

  std::atomic<size_t> m_sinkCount = { 0 };

  std::thread m_thread;

  std::vector<std::unique_ptr<SPSCSourceProcess>> m_sources;

  std::atomic<bool> m_stop = { false };

  boost::interprocess::managed_shared_memory m_memory;

  SharedMemory::Allocator m_allocator;

};

}

#endif // OLIVE_SPSC_SOURCES_H
