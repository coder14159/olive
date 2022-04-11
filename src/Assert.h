#ifndef OLIVE_ASSERT_H
#define OLIVE_ASSERT_H

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace olive {

/*
 * Asserts can be disabled at compile time
 */
#define ENABLE_ASSERTS 1

#ifdef ENABLE_ASSERTS

#define ASSERT(condition, message) do  \
{                                      \
  if (!(condition))                    \
  {                                    \
    std::cerr << message << std::endl; \
    throw std::logic_error (message);  \
  }                                    \
} while(0)


#define ASSERT_SS(condition, message) do \
{                                        \
  if (!(condition))                      \
  {                                      \
    std::ostringstream ss;               \
    ss << message;                       \
    throw std::logic_error (ss.str ());  \
  }                                      \
} while(0)

#else

#define ASSERT(condition, message) {};
#define ASSERT_SS(condition, message) {};

#endif

/*
 * Checks are always enabled
 */
#define CHECK(condition, message) do  \
{                                     \
  if (!(condition))                   \
  {                                   \
    throw std::logic_error (message); \
  }                                   \
} while(0)


#define CHECK_SS(condition, message) do \
{                                       \
  if (!(condition))                     \
  {                                     \
    std::ostringstream ss;              \
    ss << message;                      \
    throw std::logic_error (ss.str ()); \
  }                                     \
} while(0)


} // namespace olive

#endif // OLIVE_ASSERT_H
