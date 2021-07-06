#include "CpuBind.h"
#include "Logger.h"
#include "SignalCatcher.h"
#include "SPMCQueue.h"
#include "SPMCSink.h"
#include "Throttle.h"
#include "detail/CXXOptsHelper.h"
#include "detail/Utils.h"

#include <boost/interprocess/managed_shared_memory.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <numeric>

using namespace spmc;
namespace bi = boost::interprocess;

CxxOptsHelper parse (int argc, char* argv[])
{
  std::string oneKB = std::to_string (1024);
  std::string oneGB = std::to_string (1024*1024*1024);
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
     cxxopts::value<size_t> ()->default_value (oneGB))
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
  /*
   * Create enough shared memory for a single queue which is shared by all the
   * clients.
   */
  #pragma message "creation of memory should be in sink or SPMCQueue"

  size_t memory_size = queueSize
                     + SharedMemory::BOOK_KEEPING
                     + sizeof (SPMCQueue<SharedMemory::Allocator>);

  auto memory = bi::managed_shared_memory (bi::open_or_create, name.c_str(),
                                           memory_size);

  using Queue = SPMCQueue<SharedMemory::Allocator>;
  using Sink  = SPMCSink<Queue>;

  Sink sink (name, name + ":queue", queueSize);

  bool stop = { false };// TODO probably should be atomic
  /*
   * Handle signals
   */
  SignalCatcher s ({SIGINT, SIGTERM}, [&sink, &stop] (int) {

    if (!stop)
    {
      std::cout << "Stop spmc_server" << std::endl;

      sink.stop ();

      stop = true;
    }
  });

  /*
   * Create a reusable test message to send to a client
   */
  std::vector<uint8_t> message (messageSize, 0);

  std::iota (std::begin (message), std::end (message), 1);
  /*
   * If rate is not set to the maximum rate Throttle sends null messages to keep
   * the fast path warm
   */
  Throttle throttle (rate);

  while (!stop)
  {
    sink.next (message);
    /*
     * If throughput is not set to maximum rate, Throttle reduces message
     * throughput to the required rate.
     *
     * Throttle also periodically sends a WARMUP_MESSAGE_TYPE message to keep
     * the cache warm.
     */
    throttle.throttle<Sink> (sink);
  }
}

int main(int argc, char *argv[]) try
{
  auto options = parse (argc, argv);

  auto name        = options.required<std::string> ("name");
  auto messageSize = options.required<size_t>      ("message_size");
  auto queueSize   = options.required<size_t>      ("queue_size");
  auto rate        = options.required<uint32_t>    ("rate");
  auto cpu         = options.value<int>            ("cpu", -1);
  auto logLevel    = options.value<std::string>    ("log_level", log_levels (),
                                                    "INFO");

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) << "Start spmc_server";

  bind_to_cpu (cpu);

  server (name, messageSize, queueSize, rate);

  return EXIT_SUCCESS;
}
catch (const std::exception &e)
{
  std::cerr << e.what () << std::endl;
}
