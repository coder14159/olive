#include <boost/log/trivial.hpp>

#include <atomic>
#include <iomanip>
#include <memory>

namespace bi = boost::interprocess;

namespace spmc {

template <typename QueueType>
SPMCStream<QueueType>::SPMCStream (const std::string &memoryName,
                        const std::string &queueName,
                        bool allowMessageDrops,
                        size_t prefetchSize)
: m_queueObj (std::make_unique<QueueType> (memoryName, queueName)),
  m_queue (*m_queueObj)
{

  if (allowMessageDrops)
  {
    m_queue.allow_message_drops ();
  }

  if (prefetchSize > 0)
  {
    m_queue.cache_size (prefetchSize);
  }

  BOOST_LOG_TRIVIAL(info) << "SPMCStream constructed " << queueName;
}

template <typename QueueType>
SPMCStream<QueueType>::SPMCStream (QueueType &queue,
                        bool allowMessageDrops,
                        size_t prefetchSize)
: m_queue (queue)
{
  if (allowMessageDrops)
  {
    m_queue.allow_message_drops ();
  }

  if (prefetchSize > 0)
  {
    m_queue.cache_size (prefetchSize);
  }
}

template <typename QueueType>
SPMCStream<QueueType>::~SPMCStream ()
{
  if (!m_stop)
  {
    stop ();
  }

  m_queue.unregister_consumer ();
}

template <typename QueueType>
void SPMCStream<QueueType>::stop ()
{
  m_stop = true;
}

template <typename QueueType>
bool SPMCStream<QueueType>::next (Header &header, std::vector<uint8_t> &data)
{
  bool success = false;

  while (!success && !m_stop)
  {
    success = receive (header, data);
  }

  return success;
}

// TODO function placeholder for receive policies (eg backoff/yield)
template <typename QueueType>
bool SPMCStream<QueueType>::receive (Header &header, std::vector<uint8_t> &data)
{
  bool success = false;

  if (m_queue.read_available () > sizeof (Header))
  {
    success = m_queue.pop (header, data);
  }

  return success;
}

}
