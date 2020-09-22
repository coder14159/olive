namespace spmc {

/*
 * Helper class to make cxxopts syntax a little more succinct
 */
CxxOptsHelper::CxxOptsHelper (const cxxopts::ParseResult &result)
: m_result (result)
{ }

/*
 * Add positional values associated with a named command line argument
 */
void CxxOptsHelper::positional (const std::string &name,
                                const std::vector<std::string> &values)
{
  m_positional[name] = values;
}

/*
 * Return true if a positional string is associated with a named positional
 * command line argument
 */
bool CxxOptsHelper::positional (const std::string &name,
                                const std::string &value) const
{
  if (!exists (name))
  {
    return false;
  }

  return (contains_value (m_positional.at (name), value));
}

std::vector<std::string> CxxOptsHelper::values (const std::string &name) const
{
  if (!exists (name))
  {
    return {};
  }

  return m_positional.at (name);
}

/*
 * Return the value parsed from a string. Throw if not present.
 */
template<typename T>
T CxxOptsHelper::required (const std::string &name) const
{
  check_exists (name);

  return m_result[name].as<T> ();
}

/*
  * Return a default value if the name is not set in the command line
  */
template<typename T>
T CxxOptsHelper::value (const std::string &name, const T& defaultValue) const
{
  if (!exists (name))
  {
    return defaultValue;
  }

  return m_result[name].as<T> ();
}

/*
  * Get and return a value for a command line option if it appears on the list
  * of valid values, otherwise throw.
  */
template<typename T>
T CxxOptsHelper::value (const std::string &name,
                        std::vector<std::string> validValues,
                        const T& defaultValue) const
{
  auto returnValue = value (name, defaultValue);

  if (!contains_value (validValues, returnValue))
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
bool CxxOptsHelper::exists (const std::string &name) const
{
  return (m_result.count (name) != 0);
}

bool CxxOptsHelper::contains_value (const std::vector<std::string> &values,
                                    const std::string &value) const
{
  return boost::range::find (values, value) != values.end ();
}

/*
  * Assert if an argument name is not set
  */
void CxxOptsHelper::check_exists (const std::string &name) const
{
  if (!exists (name))
  {
    throw cxxopts::option_not_exists_exception (name);
  }
}

} // namespace spmc
