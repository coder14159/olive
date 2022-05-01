#include "Chrono.h"

namespace olive {

template <class Queuetype>
SPMCSource<Queuetype>::SPMCSource (size_t capacity)
: m_queue (capacity)
{ }

template <class Queuetype>
SPMCSource<Queuetype>::SPMCSource (const std::string &memoryName,
                                   const std::string &queueName,
                                   size_t             capacity)
: m_queue (memoryName, queueName, capacity)
{
  BOOST_LOG_TRIVIAL(info) << "Found or created queue named '"
    << queueName << "' with capacity of " << capacity << " bytes";
}

template <class Queuetype>
void SPMCSource<Queuetype>::stop ()
{
  m_stop = true;
}

template <class Queuetype>
void SPMCSource<Queuetype>::next (const std::vector<uint8_t> &data)
{
  Header header;
  header.size      = data.size ();
  header.seqNum    = ++m_sequenceNumber;
  header.timestamp = nanoseconds_since_epoch (Clock::now ());

  while (!m_stop && !m_queue.push (header, data))
  {
    /*
     * Re-generate the timestamp if the queue is full so that only internal
     * latency is measured
     */
    header.timestamp = nanoseconds_since_epoch (Clock::now ());
  }
}

template <class Queuetype>
template<typename POD>
void SPMCSource<Queuetype>::next (const POD &data)
{
  Header header;
  header.size      = sizeof (POD);
  header.seqNum    = ++m_sequenceNumber;
  header.timestamp = nanoseconds_since_epoch (Clock::now ());

  while (!m_stop && !m_queue.push (header, data))
  {
    /*
     * Re-generate the timestamp if the queue is full so that only internal
     * latency is measured
     */
    header.timestamp = nanoseconds_since_epoch (Clock::now ());
  }
}

template <class Queuetype>
void SPMCSource<Queuetype>::next_keep_warm ()
{
  m_queue.push (m_warmupHdr);
}

} // namespace olive

