#ifndef IPC_SPSC_SINKS_H

#include "detail/SharedMemory.h"
#include "SPSCSink.h"
#include "spmc_time.h"

#include <atomic>
#include <string>
#include <thread>

namespace spmc {

/*
 * Use SPSCSink to put data into the shared memory queue.
 *
 * Currently in prototype for testing.
 */
class SPSCSinks
{
public:
  SPSCSinks (const std::string &memoryName, size_t queueSize);

  /*
   * Send a data packet to the shared memory queue
   */
  void next (const std::vector<uint8_t> &data);

  void stop ();

  std::string name () const { return m_name; }

private:

  bool send (const uint8_t* data, size_t size);

private:

  std::string m_name;

  SharedMemory::SPSCQueue *m_requests = { nullptr };
  SharedMemory::SPSCQueue *m_response = { nullptr };

  std::atomic<size_t> m_sinkCount = { 0 };

  std::thread m_thread;

  std::vector<std::unique_ptr<SharedMemory::SPSCSink>> m_sinks;

  std::atomic<bool> m_stop = { false };

  boost::interprocess::managed_shared_memory m_memory;

  SharedMemory::Allocator m_allocator;

};

}

#endif // IPC_SPSC_SINKS_H
