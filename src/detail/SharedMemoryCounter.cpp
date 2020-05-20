#include "SharedMemoryCounter.h"

#include "Assert.h"
#include "src/detail/SharedMemory.h"

#include <boost/log/trivial.hpp>

namespace bi = boost::interprocess;

namespace spmc {

SharedMemoryCounter::SharedMemoryCounter (
  const std::string &objectName,
  const std::string &memoryName)
: m_objectName (objectName), m_memoryName (memoryName)
{
  m_memory = bi::managed_shared_memory (bi::open_only, m_memoryName.c_str());

  m_counter = m_memory.find_or_construct<CounterType> (m_objectName.c_str())();

  BOOST_LOG_TRIVIAL(debug)  << "find_or_construct object: " << m_objectName;

  ASSERT_SS (m_counter != nullptr,
             "shared memory counter initialisation failed: " << m_objectName);
}

SharedMemoryCounter::~SharedMemoryCounter ()
{
  CounterType &counter = *m_counter;

  if (counter == 0)
  {
    if (m_memory.destroy<CounterType> (m_objectName.c_str ()))
    {
      BOOST_LOG_TRIVIAL(error)  << "Failed to destroy object: " << m_objectName;
    }
  }
}

void SharedMemoryCounter::set (int value)
{
  *m_counter = value;
}

int SharedMemoryCounter::get ()
{
  return *m_counter;
}

SharedMemoryCounter &SharedMemoryCounter::operator++ ()
{
  ASSERT (m_counter != nullptr, "Uninitialised counter object");

  ++(*m_counter);

  return *this;
}

}
