#include "CpuBind.h"
#include "Logger.h"
#include "SignalCatcher.h"
#include "SPSCSink.h"
#include "Throttle.h"
#include "detail/SharedMemory.h"
#include "detail/SharedMemoryCounter.h"


#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include "detail/CXXOptsHelper.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <numeric>


using namespace std::chrono_literals;

using namespace spmc;
namespace bi = boost::interprocess;

namespace {

std::atomic<bool> g_stop = { false };

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
    ("messagesize", "Message size (bytes)",
     cxxopts::value<size_t> ()->default_value (oneKB))
    ("queuesize", "Size of queue (bytes)",
     cxxopts::value<size_t> ()->default_value (oneGB))
    ("rate", "msgs/sec (value=0 for maximum rate)",
     cxxopts::value<uint32_t> ()->default_value (rate))
    ("l,loglevel", "Logging level",
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

  SignalCatcher s({ SIGINT, SIGTERM }, [&sinks] (int) {

    g_stop = true;

    std::cout << "stopping.." << std::endl;

    for (auto &sink : sinks)
    {
      BOOST_LOG_TRIVIAL (info) << "stop " << sink->name ();

      sink->stop ();
    }
  });

  // wait for clients to be ready
  SharedMemoryCounter clientsReady (name + ":client:ready", name);

  BOOST_LOG_TRIVIAL (info) << "waiting for " << numClients << " clients..";

  int toConnect = numClients;

  while (clientsReady.get () < numClients && !g_stop)
  {
    std::this_thread::sleep_for (5ms);

    if ((numClients - clientsReady.get ()) != toConnect)
    {
      toConnect = numClients - clientsReady.get ();

      if (toConnect > 0)
      {
        BOOST_LOG_TRIVIAL (info) << "waiting for " << toConnect << " clients..";
      }
    }
  }

  if (g_stop)
  {
    return;
  }

  BOOST_LOG_TRIVIAL (info) << "start sending data";

  // create a reusable test message
  std::vector<uint8_t> message (messageSize, 0);

  std::iota (std::begin (message), std::end (message), 1);

  Throttle throttle (rate);

  while (!g_stop)
  {
    throttle.throttle ();

    for (size_t i = 0; i < sinks.size (); ++i)
    {
      sinks[i]->next (message);
    }
  }
}

} // namespace {

int main(int argc, char *argv[]) try
{
  auto options = parse (argc, argv);

  auto name        = options.required<std::string> ("name");
  auto clients     = options.required<int>         ("clients");
  auto messageSize = options.required<size_t>      ("messagesize");
  auto queueSize   = options.required<size_t>      ("queuesize");
  auto rate        = options.required<uint32_t>    ("rate");
  auto cpu         = options.value<int>            ("cpu", -1);
  auto logLevel    = options.value<std::string>    ("loglevel", log_levels (),
                                                    "INFO");

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) << "Start spsc_server";

  bind_to_cpu (cpu);

  /*
   * Create enough shared memory for each of the clients queues to fit plus some
   * extra room for shared memory booking.
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
