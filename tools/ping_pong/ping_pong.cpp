#include "Chrono.h"
#include "CpuBind.h"
#include "Latency.h"
#include "SignalCatcher.h"
#include "Timer.h"
#include "Throttle.h"
#include "Throughput.h"

#include "detail/CXXOptsHelper.h"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using namespace std::chrono;

using namespace olive;

void thread_ping_pong (Seconds timeout)
{
  std::atomic_bool stop { false };

  SignalCatcher s ({SIGINT, SIGTERM}, [&stop] (int) {
    stop = true;

    std::cout << "Stopping ping_pong" << std::endl;
  });

  const int64_t null_timestamp = 0;

  std::atomic<int64_t> ping_timestamp { null_timestamp };
  std::atomic<int64_t> pong_timestamp { null_timestamp };

  Latency latency;
  Throughput throughput;

  Timer timer;

  timer.start ();

  auto ping = std::thread ([&] {

    bind_to_cpu (1);

    int i = 0;

    while (!stop.load (std::memory_order_acq_rel))
    {
      ping_timestamp.store (
          nanoseconds_since_epoch (Clock::now ()), std::memory_order_release);

      while (pong_timestamp.load (std::memory_order_acquire) == null_timestamp)
      { }

      Nanoseconds nanoseconds (pong_timestamp - ping_timestamp);

      latency.next (nanoseconds);
      throughput.next (sizeof (i), 1);

      ++i;

      ping_timestamp.store (null_timestamp, std::memory_order_release);
      pong_timestamp.store (null_timestamp, std::memory_order_release);
    }
  });
  /*
   * Measure the time duration it takes for the pong thread to view the
   * timestamp value set by the ping thread
   */
  auto pong = std::thread ([&] {

    bind_to_cpu (2);

    while (!stop.load (std::memory_order_acq_rel))
    {
      if (pong_timestamp.load (std::memory_order_relaxed) == null_timestamp &&
          ping_timestamp.load (std::memory_order_acquire) != null_timestamp)
      {
        pong_timestamp.store (nanoseconds_since_epoch (Clock::now ()),
                              std::memory_order_release);
      }
    }
  });

  std::this_thread::sleep_for (timeout);

  stop = true;

  ping.join ();
  pong.join ();

  std::cout << "Throughput: " << throughput.to_string () << std::endl;

  for (auto s : latency.to_strings ())
  {
    std::cout << s << std::endl;
  }
}

CxxOptsHelper parse (int argc, char* argv[])
{
  std::vector<std::string> memory_types = {"interprocess", "inprocess" };

  cxxopts::Options cxxopts ("ping_pong",
    "Measure latency by bouncing a timestamp between two threads");

  cxxopts.add_options ()
    ("timeout", "Time to run the test in seconds",
      cxxopts::value<int64_t> ()->default_value ("2"))
    ("loglevel", "Logging level",
     cxxopts::value<std::string> ()->default_value ("INFO"));

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
  auto options = parse (argc, argv);

  auto timeout  = options.value<int64_t> ("timeout", 2);
  auto logLevel = options.value<std::string> ("loglevel",
                  log_levels (), "WARNING");

  set_log_level (logLevel);

  thread_ping_pong (Seconds (timeout));

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
