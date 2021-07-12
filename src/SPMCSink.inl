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
{
  BOOST_LOG_TRIVIAL(info) << "Found or created queue named '"
    << queueName << "' with capacity of " << capacity << " bytes";
}

template <class Queuetype>
void SPMCSink<Queuetype>::stop ()
{
  m_stop = true;
}

template <class Queuetype>
void SPMCSink<Queuetype>::next (const std::vector<uint8_t> &data)
{
  Header header;
  header.size      = data.size ();
  header.seqNum    = ++m_sequenceNumber;
  header.timestamp = nanoseconds_since_epoch (Clock::now ());

  while (!m_stop && !m_queue.push (header, data))
  {
    /*
     * Reset the timestamp if the queue is full so that only internal latency
     * is measured
     */
    header.timestamp = nanoseconds_since_epoch (Clock::now ());
  }
}

template <class Queuetype>
template<typename Data>
void SPMCSink<Queuetype>::next (const Data &data)
{
  Header header;
  header.size      = sizeof (data);
  header.seqNum    = ++m_sequenceNumber;
  header.timestamp = nanoseconds_since_epoch (Clock::now ());

  while (!m_stop && !m_queue.push (header, data))
  {
    /*
     * Reset the timestamp if the queue is full so that only internal latency
     * is measured
     */
    header.timestamp = nanoseconds_since_epoch (Clock::now ());
  }

}

template <class Queuetype>
void SPMCSink<Queuetype>::next_keep_warm ()
{
  m_queue.push (m_warmupHdr);
}

} // namespace spmc

