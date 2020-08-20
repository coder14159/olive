#include "detail/Utils.h"

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
  init (allowMessageDrops, prefetchSize);
}

template <typename QueueType>
SPMCStream<QueueType>::SPMCStream (QueueType &queue,
                        bool allowMessageDrops,
                        size_t prefetchSize)
: m_queue (queue)
{
  init (allowMessageDrops, prefetchSize);
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
void SPMCStream<QueueType>::init (bool allowMessageDrops, size_t prefetchSize)
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
void SPMCStream<QueueType>::stop ()
{
  m_stop = true;
}

template <typename QueueType>
bool SPMCStream<QueueType>::next (Header &header, std::vector<uint8_t> &data)
{
  while (SPMC_EXPECT_TRUE (next_non_blocking (header, data) == false))
  {
    if (m_stop.load (std::memory_order_relaxed))
    {
      return false;
    }
  }

  return true;
}

template <typename QueueType>
bool SPMCStream<QueueType>::next_non_blocking (Header &header,
                                               std::vector<uint8_t> &data)
{
  if (SPMC_EXPECT_TRUE (m_queue.read_available () > sizeof (Header)))
  {
    return m_queue.pop (header, data);
  }

  return false;
}

// TODO function placeholder for receive policies (eg backoff/yield)
template <typename QueueType>
bool SPMCStream<QueueType>::receive (Header &header, std::vector<uint8_t> &data)
{
  if (SPMC_EXPECT_TRUE (m_queue.read_available () > sizeof (Header)))
  {
    return m_queue.pop (header, data);
  }

  return false;
}

}
