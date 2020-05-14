#ifndef IPC_CXX_OPTS_HELPER
#define IPC_CXX_OPTS_HELPER

#include <cxxopts.hpp>

#include <boost/algorithm/cxx11/none_of.hpp>
#include <boost/range/algorithm.hpp>

#include <string>


namespace spmc {

/*
 * Helper class to make cxxopts syntax a little more succinct
 */
class CxxOptsHelper
{
public:
  CxxOptsHelper (const cxxopts::ParseResult &result) : m_result (result)
  { }

  /*
   * Return the parsed result object
   */
  const cxxopts::ParseResult &result () const { return m_result; }

  /*
   * Return the value parsed from a string. Throw if not present.
   */
  template<typename T>
  T required (const std::string &name) const
  {
    check_exists (name);

    return m_result[name].as<T> ();
  }


  /*
   * Return a default value if the name is not set in the command line
   */
  template<typename T>
  T value (const std::string &name, const T& defaultValue) const
  {
    if (!exists (name))
    {
      return defaultValue;
    }

    return m_result[name].as<T> ();
  }

  /*
   * Get a value for a command line option and return it if it appears on a
   * list of valid values, otherwise throw.
   */
  template<typename T>
  T value (const std::string &name,
           std::vector<std::string> validValues,
           const T& defaultValue) const
  {
    auto returnValue = value (name, defaultValue);

    if (!list_contains (returnValue, validValues))
    {
      std::stringstream ss;
      ss << "Invalid argument name: " << name << " value: " << returnValue;

      throw cxxopts::OptionParseException (ss.str ());
    }

    return returnValue;
  }

  /*
   * Return true if name is set on the command line
   */
  bool exists (const std::string &name) const
  {
    return (m_result.count (name) != 0);
  }

private:

  /*
   * Assert if an argument name is not set
   */
  void check_exists (const std::string &name) const
  {
    if (!exists (name))
    {
      throw cxxopts::option_not_exists_exception (name);
    }
  }

  /*
   * Return true if the list contains the value
   */
  template<typename T>
  bool list_contains (const std::string &value, const std::vector<T> &list) const
  {
    std::cout << value << std::endl;
    return boost::range::find (list, value) != list.end ();
  }

private:

  cxxopts::ParseResult m_result;

};

}

#endif // IPC_CXX_OPTS_HELPER
