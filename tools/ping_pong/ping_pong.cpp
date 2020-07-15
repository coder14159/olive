// #include <boost/interprocess/managed_shared_memory.hpp>
// #include <boost/log/trivial.hpp>
#include "Chrono.h"
#include "CpuBind.h"
#include "Latency.h"
#include "Logger.h"
#include "SignalCatcher.h"
#include "Timer.h"

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
    ("t,timeout", "Time to run the test in seconds (default: 2 sec)",
      cxxopts::value<int> ());

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

  auto options = parse (argc, argv);

  Seconds timeout = Seconds (options.value<int> ("timeout", 2));

  Latency latency;

  Timer timer;

  auto ns_since_epoch = [] () {
    return std::chrono::duration_cast<Nanoseconds> (
        Clock::now ().time_since_epoch ());
  };

  Nanoseconds zero_ns {0};

  timer.start ();

  Nanoseconds start_ping = zero_ns;

  /*
   * Assign a value to timestamp
   */
  auto ping = std::thread ([&] {

    bind_to_cpu (1);

    while (!stop)
    {
      if (start_ping == zero_ns)
      {
        start_ping = ns_since_epoch ();

        // std::atomic_thread_fence (std::memory_order_release);
        std::atomic_thread_fence (std::memory_order_relaxed);
      }
      else
      {
        // std::this_thread::sleep_for (5ms);
      }

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
      // std::atomic_thread_fence (std::memory_order_relaxed);
      std::atomic_thread_fence (std::memory_order_acquire);

      if (start_ping != zero_ns)
      {
        latency.latency ((ns_since_epoch () - start_ping).count ());

        ++count;

        start_ping = zero_ns;

        std::atomic_thread_fence (std::memory_order_release);
        // std::atomic_thread_fence (std::memory_order_relaxed);

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
