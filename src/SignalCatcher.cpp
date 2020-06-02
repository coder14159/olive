#include "SignalCatcher.h"
#include <csignal>

namespace spmc {
namespace {

std::function<void (int)> shutdown_handler;

void signal_handler (int signal) { shutdown_handler (signal); }

} // namespace {

SignalCatcher::SignalCatcher (
  const std::vector<int> &signals,
  std::function<void (int)> handler)
: m_signals (signals)
{
  shutdown_handler = handler;

  for (auto signal : signals)
  {
    std::signal (signal, signal_handler);
  }
}

} // namespace spmc {
