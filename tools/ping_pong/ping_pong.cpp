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

    std::cout << "stopping.." << std::endl;
  });

  auto ns_since_epoch = [] () {
    return std::chrono::duration_cast<Nanoseconds> (
        Clock::now ().time_since_epoch ()).count ();
  };

  auto options = parse (argc, argv);

  auto sleep = Nanoseconds (options.value<int64_t> ("pause", 100));

  auto timeout = Seconds (options.value<int64_t> ("timeout", 2));

  auto rate = options.value<uint64_t> ("rate", 0);

  int64_t zero_ns {0};

  std::atomic<int64_t> start_nanos { zero_ns };

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
      if (start_nanos == zero_ns)
      {
        start_nanos = ns_since_epoch ();
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
      if (start_nanos != zero_ns)
      {
        latency.next (ns_since_epoch () - start_nanos);

        ++count;

        start_nanos = zero_ns;
      }
    }
  });

  ping.join ();
  pong.join ();

  std::cout << "count: " << count << std::endl;

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
