#include "CpuBind.h"
#include "Logger.h"
#include "SignalCatcher.h"
#include "SPSCSink.h"
#include "Throttle.h"
#include "detail/CXXOptsHelper.h"
#include "detail/SharedMemory.h"
#include "detail/SharedMemoryCounter.h"
#include "detail/Utils.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <numeric>


using namespace std::chrono_literals;

using namespace spmc;
namespace bi = boost::interprocess;

namespace {

CxxOptsHelper parse (int argc, char* argv[])
{
  std::string oneKB = std::to_string (1024);
  std::string oneGB = std::to_string (1024*1024*1024);
  std::string level = "NOTICE";
  std::string rate  = "0";
  std::string cpu   = "-1";

  cxxopts::Options cxxopts ("spsc_server",
        "Message producer for SPSC shared memory performance testing");

  cxxopts.add_options ()
    ("h,help", "Message producer for shared memory performance testing")
    ("name", "Shared memory name", cxxopts::value<std::string> ())
    ("clients", "Number of consumer clients", cxxopts::value<int> ())
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
             int                numClients,
             size_t             messageSize,
             size_t             queueSize,
             uint32_t           rate)
{
  // define the number of clients to service
  SharedMemoryCounter clientCount (name + ":client:count", name);

  clientCount.set (numClients);

  // TODO a single multi sink could encapsulate sending to all streams

  // initialise the sinks for sending
  std::vector<std::unique_ptr<SPSCSink>> sinks;

  for (int i = 1; i < numClients+1; ++i)
  {
    std::string objectName = name + ":sink:" + std::to_string (i);

    auto sink = std::make_unique<SPSCSink> (name, objectName, queueSize);

    sinks.push_back (std::move (sink));
  }

  bool stop { false };

  SignalCatcher s({ SIGINT, SIGTERM }, [&sinks, &stop] (int) {

    stop = true;

    for (auto &sink : sinks)
    {
      BOOST_LOG_TRIVIAL (info) << "Stop " << sink->name ();

      sink->stop ();
    }

    std::cout << "Stopping spsc_server" << std::endl;
 });

  // wait for clients to be ready
  SharedMemoryCounter clientsReady (name + ":client:ready", name);

  BOOST_LOG_TRIVIAL (info) << "Waiting for " << numClients << " clients..";

  int toConnect = numClients;

  while (clientsReady.get () < numClients && !stop)
  {
    std::this_thread::sleep_for (5ms);

    if ((numClients - clientsReady.get ()) != toConnect)
    {
      toConnect = numClients - clientsReady.get ();

      if (toConnect > 0)
      {
        BOOST_LOG_TRIVIAL (info) << "Waiting for " << toConnect << " clients..";
      }
    }
  }

  BOOST_LOG_TRIVIAL (info) << numClients << " client ready";

  // create a reusable test message
  std::vector<uint8_t> message (messageSize, 0);

  std::iota (std::begin (message), std::end (message), 1);

  Throttle throttle (rate);

  while (SPMC_EXPECT_FALSE (!stop))
  {
    for (auto &sink : sinks)
    {
      sink->next (message);
    }
    throttle.throttle<SPSCSink> (*sinks.back ());
  }
}

} // namespace {

int main(int argc, char *argv[]) try
{
  auto options = parse (argc, argv);

  auto name        = options.required<std::string> ("name");
  auto clients     = options.required<int>         ("clients");
  auto messageSize = options.required<size_t>      ("message_size");
  auto queueSize   = options.required<size_t>      ("queue_size");
  auto rate        = options.required<uint32_t>    ("rate");
  auto cpu         = options.value<int>            ("cpu", -1);
  auto logLevel    = options.value<std::string>    ("log_level", log_levels (),
                                                    "INFO");

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) << "Start spsc_server";

  bind_to_cpu (cpu);

  /*
   * Create enough shared memory for each of the clients queues to fit plus some
   * extra room for shared memory book keeping.
   */
  size_t memorySize = (queueSize * clients)
                    + (SharedMemory::BOOK_KEEPING*clients);

  auto memory = bi::managed_shared_memory (bi::open_or_create, name.c_str(),
                                           memorySize);

  server (name, clients, messageSize, queueSize, rate);

  return EXIT_SUCCESS;
}
catch (const std::exception &e)
{
  std::cerr << e.what () << std::endl;
}
