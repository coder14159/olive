#include "CpuBind.h"
#include "Logger.h"
#include "SignalCatcher.h"
#include "SPMCQueue.h"
#include "SPMCSource.h"
#include "Throttle.h"
#include "detail/CXXOptsHelper.h"
#include "detail/Utils.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <numeric>

using namespace olive;
namespace bi = boost::interprocess;

CxxOptsHelper parse (int argc, char* argv[])
{
  std::string oneKB = std::to_string (1024);
  std::string oneMB = std::to_string (1024*1024);
  std::string level = "NOTICE";
  std::string rate  = "0";
  std::string cpu   = "-1";

  cxxopts::Options cxxopts ("spmc_server",
        "Message producer for shared memory performance testing");

  cxxopts.add_options ()
    ("h,help", "Produce messages for shared memory performance testing")
    ("name", "Shared memory name", cxxopts::value<std::string> ())
    ("message_size", "Message size (bytes)",
     cxxopts::value<size_t> ()->default_value (oneKB))
    ("queue_size", "Size of queue (bytes)",
     cxxopts::value<size_t> ()->default_value (oneMB))
    ("rate", "msgs/sec (value=0 for maximum rate)",
     cxxopts::value<uint32_t> ()->default_value (rate))
    ("l,log_level", "Logging level",
     cxxopts::value<std::string> ()->default_value (level))
    ("cpu", "bind main thread to a cpu processor id",
     cxxopts::value<int> ()->default_value (cpu));

  CxxOptsHelper options (cxxopts.parse (argc, argv));

  if (options.exists ("help"))
  {
    std::cout << cxxopts.help ({"", "Group"}) << std::endl;

    exit (EXIT_SUCCESS);
  }

  return options;
}

void server (const std::string& name,
             size_t             messageSize,
             size_t             queueSize,
             uint32_t           rate)
{
  BOOST_LOG_TRIVIAL (info) << "Target message rate: "
                           << ((rate == 0) ? "max" : std::to_string (rate));
  /*
   * Create enough shared memory for a single queue which is shared by all the
   * clients.
   */
  using Queue  = SPMCQueue<SharedMemory::Allocator>;
  using Source = SPMCSource<Queue>;

  Source source (name, name + ":queue", queueSize);

  std::atomic<bool> stop = { false };
  /*
   * Handle signals
   */
  SignalCatcher s ({SIGINT, SIGTERM}, [&source, &stop] (int) {

    if (!stop)
    {
      BOOST_LOG_TRIVIAL (debug) << "Stop spmc_server";

      stop = true;

      source.stop ();
    }
  });

  /*
   * Create a reusable test message to send to a client
   */
  std::vector<uint8_t> message (messageSize, 0);

  std::iota (std::begin (message), std::end (message), 1);

  if (rate == 0)
  {
    while (SPMC_EXPECT_TRUE (!stop.load (std::memory_order_relaxed)))
    {
      source.next (message);
    }
  }
  else
  {
   /*
    * If rate is not set to the maximum rate throttle sends null messages to
    * keep the fast path warm
    */
    Throttle throttle (rate);

    while (SPMC_EXPECT_TRUE (!stop.load (std::memory_order_relaxed)))
    {
      source.next (message);
      /*
       * Throttle also periodically sends a WARMUP_MESSAGE_TYPE message to keep
       * the cache warm.
       */
      throttle.throttle<Source> (source);
    }
  }
}

int main(int argc, char *argv[]) try
{
  auto options = parse (argc, argv);

  auto name        = options.required<std::string> ("name");
  auto messageSize = options.required<size_t>      ("message_size");
  auto queueSize   = options.required<size_t>      ("queue_size");
  auto rate        = options.value<uint32_t>       ("rate", 0);
  auto cpu         = options.value<int>            ("cpu", -1);
  auto logLevel    = options.value<std::string>    ("log_level", log_levels (),
                                                    "INFO");

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) << "Start spmc_server";

  bind_to_cpu (cpu);

  server (name, messageSize, queueSize, rate);

  BOOST_LOG_TRIVIAL (info) << "Exit spmc_server";

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
catch (const std::exception &e)
{
  std::cerr << e.what () << std::endl;
}
