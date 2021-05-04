#include "Logger.h"

#include <boost/algorithm/string.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/logger.hpp>

namespace log = boost::log;

namespace spmc {

namespace {
  static boost::log::trivial::severity_level LOG_LEVEL = log::trivial::info;
}

std::vector<std::string> log_levels ()
{
  return { "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL" };
}

boost::log::trivial::severity_level get_log_level ()
{
  return LOG_LEVEL;
}

void set_log_level (boost::log::trivial::severity_level level)
{
  log::core::get ()->set_filter (log::trivial::severity >= level);
  LOG_LEVEL = level;
}

void set_log_level (const std::string &level)
{
  auto log_level = boost::to_upper_copy (level);

  if      (log_level == "TRACE"  ) { set_log_level (log::trivial::trace);   }
  else if (log_level == "DEBUG"  ) { set_log_level (log::trivial::debug);   }
  else if (log_level == "INFO"   ) { set_log_level (log::trivial::info);    }
  else if (log_level == "WARNING") { set_log_level (log::trivial::warning); }
  else if (log_level == "ERROR"  ) { set_log_level (log::trivial::error);   }
  else if (log_level == "FATAL"  ) { set_log_level (log::trivial::fatal);   }
  else
  {
  	std::string message = "Invalid log level: " + log_level;
  	throw std::invalid_argument (message);
  }
}

} // namespace spmc
