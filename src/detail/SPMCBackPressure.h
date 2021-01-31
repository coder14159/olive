#ifndef IPC_DETAIL_SPMC_BACK_PRESSURE_H
#define IPC_DETAIL_SPMC_BACK_PRESSURE_H

#include "detail/SharedMemory.h"

#include <array>
#include <atomic>

namespace spmc {

/*
 * SPMCBackPressure manages the registration and unregistration of consumer
 * threads or processes with the queue.
 *
 * It exerts back pressure on the producer if required.
 */
template<class Mutex, uint8_t MaxNoDropConsumers = MAX_NO_DROP_CONSUMERS_DEFAULT>
class SPMCBackPressure
{
public:
  SPMCBackPressure ();

  /*
   * Returns the bytes consumed by the slowest consumer
   */
  uint64_t min_consumed ();

  /*
   * Return true if there are any consumers configured not to drop messages
   */
  bool has_non_drop_consumers () const;

  /*
   * If a consumer is registered with the producer then back-pressure is exerted
   * by the consumer so that message dropping is prevented.
   *
   * returns a consumer specific index value
   */
  size_t register_consumer ();

  /*
   * Unregister a consumer when it stops.
   */
  void unregister_consumer (size_t index);

  void reset_consumers ();

  /*
   * Used by the consumer to inform the producer how many bytes it has consumed
   * - back pressure.
   *
   * Only used by consumers configured not to drop messages.
   */
  void consumed (uint64_t bytes, size_t index);

protected:

  /*
   * Mutex used to register/unregister new consumer threads
   */
  Mutex m_mutex;

  /*
   * Count of consumers not allowed to drop messages
   */
  uint8_t m_maxConsumerIndex = { 0 };
  /*
   * Array holding the bytes consumed for each non message dropping consumer
   *
   * TODO: Maybe use boost::small_vector
   */
  alignas (CACHE_LINE_SIZE)
  std::array<uint64_t, MaxNoDropConsumers> m_consumed;

  /*
   * True if there are any non message dropping consumers
   */
  std::atomic<bool> m_hasNonDropConsumers = { false };

  /*
   * The maximum number of non message dropping consumers configured
   */
  uint8_t m_maxNoDropConsumers = { 0 };

  /*
   * Current number of consumers
   */
  uint8_t m_consumerCount = { 0 };

  /*
   * Index used to implement fair servicing of the ConsumerArray
   */
  uint8_t m_lastIndex = { 0 };

};

} // namespace spmc {

#include "detail/SPMCBackPressure.inl"

#endif // IPC_DETAIL_SPMC_BACK_PRESSURE_H
