#ifndef IPC_LOGGER_H
#define IPC_LOGGER_H

#include <boost/log/trivial.hpp>

namespace spmc {

boost::log::trivial::severity_level get_log_level ();

void set_log_level (boost::log::trivial::severity_level level);

void set_log_level (const std::string &level);

#define TRACE_ENABLED 0

} // namespace spmc

#endif // IPC_LOGGER_H

