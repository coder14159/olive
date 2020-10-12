#include "SPMCQueue.h"
#include "detail/SharedMemory.h"

#include <boost/thread/tss.hpp>
#include <limits>

namespace spmc {

constexpr size_t   Consumer::UnInitialisedIndex;
constexpr uint64_t Consumer::UnInitialised;
constexpr uint64_t Consumer::Stopped;

namespace detail {

uint64_t &get_bytes ()
{
  thread_local uint64_t bytes = Consumer::UnInitialised;
  return bytes;
}

bool &get_initialised ()
{
  thread_local bool initialised = false;
  return initialised;
}

bool &get_message_drops_allowed ()
{
  thread_local bool message_drops_allowed = false;
  return message_drops_allowed;
}

InprocessConsumer::~InprocessConsumer ()
{
  /*
   * Reset thread local variables explicitly as some unit tests are run from
   * a single thread.
   */
  get_bytes () = Consumer::UnInitialised;

  get_initialised ()  = false;

  get_message_drops_allowed () = false;
}

bool InprocessConsumer::initialised ()
{
  return (get_bytes () != Consumer::UnInitialised);
}

uint64_t &InprocessConsumer::consumed ()
{
  return get_bytes ();
}

uint64_t InprocessConsumer::consumed () const
{
  return get_bytes ();
}

void InprocessConsumer::consumed (uint64_t consumed)
{
  get_bytes () = consumed;
}

void InprocessConsumer::add (uint64_t consumed)
{
  get_bytes () += consumed;
}

bool InprocessConsumer::message_drops_allowed () const
{
  return get_message_drops_allowed ();
}

void InprocessConsumer::allow_message_drops ()
{
  get_message_drops_allowed () = true;
}


////////////////////////////////////////////////////////////////////////////////
size_t &get_index ()
{
  thread_local size_t index = Consumer::UnInitialisedIndex;
  return index;
}

InprocessProducer::~InprocessProducer ()
{
  /*
   * Reset thread local variables explicitly as some unit tests are run from
   * a single thread.
   */
  get_index () = Consumer::UnInitialisedIndex;
}

size_t InprocessProducer::index () const
{
  return get_index ();
}

void InprocessProducer::index (size_t index)
{
  /*
   * initialise the current consumer thread not to drop messages
   */
  get_index () = index;
}

} // namespace detail {
} // namespace spmc {