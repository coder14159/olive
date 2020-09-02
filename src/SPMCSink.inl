#include "Chrono.h"

namespace spmc {

template <class Queuetype>
SPMCSink<Queuetype>::SPMCSink (size_t capacity)
: m_queue (capacity)
{ }

template <class Queuetype>
SPMCSink<Queuetype>::SPMCSink (const std::string &memoryName,
                               const std::string &queueName,
                               size_t             capacity)
: m_queue (memoryName, queueName, capacity)
{ }

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
  header.timestamp = nanoseconds_since_epoch (Clock::now ());

  while (true)
  {
    if (m_stop.load (std::memory_order_relaxed) || m_queue.push (header, data))
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
  header.timestamp = nanoseconds_since_epoch (Clock::now ());

  while (true)
  {
    if (m_stop.load (std::memory_order_relaxed) || m_queue.push (header, data))
    {
      break;
    }
  }
}

template <class Queuetype>
void SPMCSink<Queuetype>::next_keep_warm ()
{
  m_queue.push (m_warmupHdr, m_warmupMsg);
}

} // namespace spmc

