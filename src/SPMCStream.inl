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
    m_queue.resize_cache (prefetchSize);
  }

  m_queue.register_consumer ();
}

template <typename QueueType>
void SPMCStream<QueueType>::stop ()
{
  m_stop = true;
}

template <typename QueueType>
template<typename Vector>
bool SPMCStream<QueueType>::next (Header &header, Vector &data)
{
  while (!m_stop.load (std::memory_order_relaxed))
  {
    if (SPMC_EXPECT_TRUE (m_queue.pop (header, data) &&
                          header.type != WARMUP_MESSAGE_TYPE))
    {
      return true;
    }
  }

  return false;
}

template <typename QueueType>
bool SPMCStream<QueueType>::next (Header &header,
                                  Buffer<std::allocator<uint8_t>> &data)
{
  while (!m_stop.load (std::memory_order_relaxed))
  {
    if (SPMC_EXPECT_TRUE (m_queue.pop (header, data)))
    {
      return true;
    }
  }

  return false;
}

template <typename QueueType>
template<typename Vector>
bool SPMCStream<QueueType>::next_non_blocking (Header &header, Vector &data)
{
  return (SPMC_EXPECT_TRUE (m_queue.pop (header, data)));
}

// TODO receive policies (eg backoff/yield)
template <typename QueueType>
bool SPMCStream<QueueType>::receive (Header &header, std::vector<uint8_t> &data)
{
  return (SPMC_EXPECT_TRUE (m_queue.pop (header, data)));
}

}
