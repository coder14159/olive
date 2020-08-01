#ifndef IPC_ASSERT_H
#define IPC_ASSERT_H

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace spmc {

#ifdef ENABLE_ASSERTS

#define ASSERT(condition, message) do  \
{                                      \
  if (!(condition))                    \
  {                                    \
    std::cerr << message << std::endl; \
    throw std::logic_error (message);  \
  }                                    \
} while(0)


#define ASSERT_SS( condition, message) do \
{                                         \
  if (!(condition))                       \
  {                                       \
    std::ostringstream ss;                \
    ss << message;                        \
    throw std::logic_error (ss.str ());   \
  }                                       \
} while(0)

#else

#define ASSERT(condition, message) {};
#define ASSERT_SS(condition, message) {};

#endif

#define UNREACHABLE(message) { throw (message); }

} // namespace spmc

#endif // IPC_ASSERT_H
