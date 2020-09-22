#ifndef IPC_CXX_OPTS_HELPER
#define IPC_CXX_OPTS_HELPER

#include <cxxopts.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/range/algorithm.hpp>

#include <map>
#include <set>
#include <string>


namespace spmc {

/*
 * Helper class to make cxxopts syntax a little more succinct
 */
class CxxOptsHelper
{
public:
  CxxOptsHelper (const cxxopts::ParseResult &result);

  /*
   * Add positional values associated with a named command line argument
   */
  void positional (const std::string &name,
                   const std::vector<std::string> &values);

  /*
   * Return true if a positional string is associated with a named positional
   * command line argument
   */
  bool positional (const std::string &name, const std::string &value) const;

   /*
   * Return the value parsed from a string. Throw if not present.
   */
  template<typename T>
  T required (const std::string &name) const;

  /*
   * Return a default value if the name is not set in the command line
   */
  template<typename T>
  T value (const std::string &name, const T& defaultValue) const;

  /*
   * Get and return a value for a command line option if it appears on the list
   * of valid values, otherwise throw.
   */
  template<typename T>
  T value (const std::string &name,
           std::vector<std::string> validValues,
           const T& defaultValue) const;

  /*
   * Return a list of values associated with an option name
   */
  std::vector<std::string> values (const std::string &name) const;

  /*
   * Return true if name is set on the command line
   */
  bool exists (const std::string &name) const;

private:
  bool contains_value (const std::vector<std::string> &values,
                       const std::string &value) const;

  /*
   * Assert if an argument name is not set
   */
  void check_exists (const std::string &name) const;


private:

  cxxopts::ParseResult m_result;

  std::map<std::string, std::vector<std::string>> m_positional;

};

}

#include "CXXOptsHelper.inl"

#endif // IPC_CXX_OPTS_HELPER
