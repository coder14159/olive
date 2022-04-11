#ifndef OLIVE_DETAIL_SHARED_MEMORY_COUNTER_H
#define OLIVE_DETAIL_SHARED_MEMORY_COUNTER_H

#include "detail/SharedMemory.h"

#include <boost/core/noncopyable.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <atomic>
#include <string>

namespace olive {

/*
 * A shared memory counter.
 *
 * The counter stays resident in shared memory on destruction unless the counter
 * value is zero.
 */
class SharedMemoryCounter : private boost::noncopyable
{
public:

  using CounterType = std::atomic<int>;

public:
  SharedMemoryCounter () = delete;

  SharedMemoryCounter (const std::string &name, const std::string &memoryName);

  ~SharedMemoryCounter ();

  SharedMemoryCounter &operator++ ();

  // set the counter value
  void set (int value);

  // get the counter value
  int get ();

private:
  std::string  m_objectName;
  std::string  m_memoryName;

  CounterType *m_counter = { nullptr };

  boost::interprocess::managed_shared_memory m_memory;
};

}

#endif // OLIVE_DETAIL_SHARED_MEMORY_COUNTER_H
