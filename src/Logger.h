#ifndef IPC_LOGGER_H
#define IPC_LOGGER_H

#include <boost/log/trivial.hpp>

#include <vector>

namespace spmc {

boost::log::trivial::severity_level get_log_level ();

void set_log_level (boost::log::trivial::severity_level level);

void set_log_level (const std::string &level);

std::vector<std::string> log_levels ();

class ScopedLogLevel
{
public:
  ScopedLogLevel (boost::log::trivial::severity_level level) : m_level (level)
  {
    set_log_level (level);
  }

  ~ScopedLogLevel ()
  {
    set_log_level (m_level);
  }

private:
  boost::log::trivial::severity_level m_level;
};

#define TRACE_ENABLED 0

} // namespace spmc

#endif // IPC_LOGGER_H

