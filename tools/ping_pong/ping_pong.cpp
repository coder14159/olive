#include "Chrono.h"
#include "CpuBind.h"
#include "Latency.h"
#include "Logger.h"
#include "SignalCatcher.h"
#include "Timer.h"
#include "Throttle.h"

#include "detail/CXXOptsHelper.h"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using namespace std::chrono;

using namespace spmc;

CxxOptsHelper parse (int argc, char* argv[])
{
  cxxopts::Options cxxopts ("ping_pong", "Measure cpu latency");

  cxxopts.add_options ()
    ("h,help", "Measure inter-CPU latency")
    ("r,rate", "msgs/sec set rate to 0 for maximum rate (default: 0)",
      cxxopts::value<uint64_t> ())
    ("t,timeout", "Time to run the test in seconds (default: 2 sec)",
      cxxopts::value<int64_t> ());

  CxxOptsHelper options (cxxopts.parse (argc, argv));

  if (options.exists ("help"))
  {
    std::cout << cxxopts.help ({"", "Group"}) << std::endl;

    exit (EXIT_SUCCESS);
  }

  return options;
}

int main(int argc, char* argv[]) try
{
  set_log_level ("WARNING");

  std::atomic_bool stop { false };

  SignalCatcher s ({SIGINT, SIGTERM}, [&stop] (int) {
    stop = true;

    std::cout << "Stopping ping_pong" << std::endl;
  });

  auto ns_since_epoch = [] () {
    return std::chrono::duration_cast<Nanoseconds> (
        Clock::now ().time_since_epoch ()).count ();
  };

  auto options = parse (argc, argv);

  auto timeout = Seconds (options.value<int64_t> ("timeout", 2));

  auto rate = options.value<uint64_t> ("rate", 0);

  const int64_t zero = 0;

  std::atomic<int64_t> ping_timestamp { zero };

  Latency latency;

  Throttle throttle (rate);

  Timer timer;

  timer.start ();
  /*
   * Assign a value to timestamp
   */
  auto ping = std::thread ([&] {

    bind_to_cpu (1);

    while (!stop)
    {
      if (ping_timestamp == zero)
      {
        ping_timestamp = ns_since_epoch ();
      }

      throttle.throttle ();

      if (to_seconds (timer.elapsed ()) > timeout.count ())
      {
        stop = true;
      }
    }
  });

  uint64_t count = 0;

  /*
   * Measure the time duration it takes for the pong thread to view the
   * timestamp value set by the ping thread
   */
  auto pong = std::thread ([&] {

    bind_to_cpu (3);

    while (!stop)
    {
      int64_t p = ping_timestamp;

      if (p != zero)
      {
        Nanoseconds duration (ns_since_epoch () - p);
        latency.next (duration);

        ++count;

        ping_timestamp = zero;
      }
    }
  });

  ping.join ();
  pong.join ();

  for (auto s : latency.to_strings ())
  {
    std::cout << s << std::endl;
  }

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
