#include "Logger.h"
#include "detail/SharedMemory.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <atomic>

namespace bi = boost::interprocess;
using namespace spmc;

namespace spmc {

template <class Queuetype>
SPMCSink<Queuetype>::SPMCSink (size_t capacity)
: m_queue (capacity)
{
}

template <class Queuetype>
SPMCSink<Queuetype>::SPMCSink (const std::string &memoryName,
                               const std::string &queueName,
                               size_t             capacity)
: m_queue (memoryName, queueName, capacity)
{
}

template <class Queuetype>
void SPMCSink<Queuetype>::stop ()
{
  m_stop = true;
}

template <class Queuetype>
void SPMCSink<Queuetype>::next (const std::vector<uint8_t> &data)
{
  /*
   * Sending data to the queue never fails, slow consumers may drop data unless
   * no drops is enabled for one or more consumer threads/processes.
   */
  Header header;
  header.size      = data.size ();
  header.seqNum    = ++m_sequenceNumber;
  header.timestamp = Time::now ().serialise ();

  while (true)
  {
    if (m_stop || m_queue.push (header, data))
    {
      break;
    }
  }
}

template <class Queuetype>
template<typename POD>
void SPMCSink<Queuetype>::next (const POD &data)
{

  /*
   * Sending data to the queue never fails, slow consumers may drop data unless
   * no drops is enabled for one or more consumer threads/processes.
   */
  Header header;
  header.size      = sizeof (data);
  header.seqNum    = ++m_sequenceNumber;
  header.timestamp = Time::now ().serialise ();

  while (true)
  {
    if (m_stop || m_queue.push (header, data))
    {
      break;
    }
  }
}

}
