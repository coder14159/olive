#include "CpuBind.h"
#include "Logger.h"
#include "SignalCatcher.h"
#include "SPSCSource.h"
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

using namespace olive;
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

  // TODO a single multi source could encapsulate sending to all streams

  // initialise the sources for sending
  std::vector<std::unique_ptr<SPSCSourceProcess>> sources;

  for (int i = 1; i <= numClients; ++i)
  {
    auto objectName = name + ":source:" + std::to_string (i);

    auto source = std::make_unique<SPSCSourceProcess> (name, objectName, queueSize);

    sources.push_back (std::move (source));
  }

  std::atomic<bool> stop = { false };

  SignalCatcher s({ SIGINT, SIGTERM }, [&sources, &stop] (int) {

    stop = true;

    BOOST_LOG_TRIVIAL (debug) << "Stop spsc_server";

    for (auto &source : sources)
    {
      BOOST_LOG_TRIVIAL (info) << "Stop " << source->name ();

      source->stop ();
    }
  });

  // wait for clients to be ready
  SharedMemoryCounter clientsReady (name + ":client:ready", name);

  int toConnect = numClients;

  auto log_waiting_for_clients = [&toConnect] () {
    BOOST_LOG_TRIVIAL (info) << "Waiting for " << toConnect
                             << ((toConnect > 1) ? " clients.." : " client..");
  };

  log_waiting_for_clients ();

  while (clientsReady.get () < numClients && !stop)
  {
    std::this_thread::sleep_for (1ms);

    if ((numClients - clientsReady.get ()) != toConnect)
    {
      toConnect = numClients - clientsReady.get ();
      if (toConnect > 0)
      {
        log_waiting_for_clients ();
      }
    }
  }

  BOOST_LOG_TRIVIAL (info)
    << ((numClients > 1) ? (std::to_string (numClients) + " clients")
                         : "Client")
    << " ready";

  // create a reusable test message
  std::vector<uint8_t> message (messageSize, 0);

  std::iota (std::begin (message), std::end (message), 1);

  size_t first = 0;
  size_t sourceCount = sources.size ();

  /*
   * For fair data distribution of message latencies rotate the ordering the
   * that clients receives data.
   */
  if (rate == 0)
  {
    while (!stop.load (std::memory_order_relaxed))
    {
      for (size_t i = first; i < first + sourceCount; ++i)
      {
        sources[MODULUS (i, sourceCount)]->next (message);
      }

      first = ((first + 1) < sourceCount) ? first + 1 : 0;
    }
  }
  else
  {
    /*
     * If rate is not set to the maximum throttle sends null messages to
     * keep the fast path warm.
     *
     * Throttle also periodically sends a WARMUP_MESSAGE_TYPE message to keep
     * the cache warm.
     */
    Throttle throttle (rate);

    while (!stop.load (std::memory_order_relaxed))
    {
      for (size_t i = first; i < first + sourceCount; ++i)
      {
        sources[MODULUS (i, sourceCount)]->next (message);

        /*
         * SPSC queues perform better if warmup messages are not sent.
         * This is in contrast to the SPMC behaviour.
         */
      }

      first = ((first + 1) < sourceCount) ? first + 1 : 0;

      throttle.throttle ();
    }
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
