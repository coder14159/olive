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

void interthread_atomic_ping_pong (Seconds timeout)
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

  Timer timer;

  timer.start ();

  auto ping = std::thread ([&] {

    bind_to_cpu (1);

    int i = 0;

    while (true)
    {
      ping_timestamp = nanoseconds_since_epoch (Clock::now ());

      while (pong_timestamp == null_timestamp)
      { }

      Nanoseconds nanoseconds (pong_timestamp - ping_timestamp);

      latency.next (nanoseconds);

      ++i;

      if (stop)
      {
        break;
      }

      ping_timestamp = null_timestamp;
      pong_timestamp = null_timestamp;
    }

    std::cout << "count: " << i << std::endl;
  });
  /*
   * Measure the time duration it takes for the pong thread to view the
   * timestamp value set by the ping thread
   */
  auto pong = std::thread ([&] {

    bind_to_cpu (2);

    while (!stop)
    {
      if (pong_timestamp == null_timestamp && ping_timestamp != null_timestamp)
      {
        pong_timestamp = nanoseconds_since_epoch (Clock::now ());
      }
    }
  });

  std::this_thread::sleep_for (timeout);

  stop = true;

  ping.join ();
  pong.join ();

  for (auto s : latency.to_strings ())
  {
    std::cout << s << std::endl;
  }
}

void interprocess_ping_pong ()
{

}

CxxOptsHelper parse (int argc, char* argv[])
{
  std::vector<std::string> memory_types = {"interprocess", "inprocess" };

  cxxopts::Options cxxopts ("ping_pong",
    "Measure latency of a shared atomic or full sink/streams");

  cxxopts.add_options ()
    ("h,help", "Measure inter-CPU latency")
    ("type", "Execute using inprocess threads or processes over shared memory",
     cxxopts::value<std::string> ())
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
  auto type     = options.value<std::string> ("type",
                  {"threads", "processes"}, "threads");

  set_log_level (logLevel);

  if (type == "processes")
  {


  }
  else
  {
    interthread_atomic_ping_pong (Seconds (timeout));
  }

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
