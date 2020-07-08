#ifndef IPC_ASSERT_H
#define IPC_ASSERT_H

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace spmc {

template<typename ExceptionType>
void check (const char *expr_str, bool expr, const std::string &message)
{
  if (!expr)
  {
    std::stringstream ss;

    ss << expr_str << "\nCheck failed: " << message;

    throw ExceptionType (ss.str ());
  }
}

#define CHECK_SS(expr, message) \
{ \
  if (expr) { return; } \
  \
  std::stringstream ss; \
  ss << message; \
  check<std::logic_error> (#expr, expr, ss.str ().c_str ()); \
}

/*
 * If assert expression is false, invoke the lambda function then abort
 */
template<typename Lambda>
void assert_expr (bool expr, Lambda lambda)
{
  if (!expr)
  {
    lambda ();
    abort ();
  }
}

#ifdef SPMC_ENABLE_ASSERTS

inline
void assert_function (
  const char *expr_str,
  bool expr,
  const char *message,
  const char *file,
  int line)
{
  if (!expr)
  {
    std::cerr << "Assert failed: " << expr_str
              << " (" << file << ":" << line << ")" << std::endl;
    abort ();
  }
}

  /*
   * Note: no space between macro name and bracket is required
   */

  #define ASSERT(expr, message) \
    assert_function (#expr, expr, message, __FILE__, __LINE__);

  #define ASSERT_SS(expr, message) \
  { \
    std::stringstream ss; \
    ss << message; \
    assert_function (#expr, expr, ss.str ().c_str (), __FILE__, __LINE__); \
  }

#else

  #define ASSERT(expression, message) ;
  #define ASSERT_SS(expression, message) ;

#endif // SPMC_ENABLE_ASSERTS

} // namespace spmc

#endif // IPC_ASSERT_H
