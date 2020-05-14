#ifndef IPC_ASSERT_H
#define IPC_ASSERT_H

#include <iostream>
#include <sstream>

namespace spmc {

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
