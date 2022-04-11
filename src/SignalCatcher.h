#ifndef OLIVE_DETAIL_SIGNAL_CATCHER_H
#define OLIVE_DETAIL_SIGNAL_CATCHER_H

#include <csignal>
#include <functional>
#include <vector>

namespace olive {

/*
 * A signal catcher which accepts a function object or lambda with capture
 * syntax
 */
class SignalCatcher
{
public:
  SignalCatcher (const std::vector<int> &signals,
                 std::function<void (int)> action);

private:

  std::vector<int> m_signals;

};

} // namespace olive {

#endif // OLIVE_DETAIL_SIGNAL_CATCHER_H
