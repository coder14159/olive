#include "CpuBind.h"
#include "Logger.h"
#include "detail/SharedMemory.h"
#include "SPSCSink.h"
#include "Throttle.h"
#include "detail/SharedMemoryCounter.h"

#include "spmc_argparse.h"
#include "spmc_signal_catcher.h"
#include "spmc_timer.h"
#include "spmc_time_duration.h"

#include "patch/lockfree/spsc_queue.hpp"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <atomic>
#include <memory>

using namespace spmc;
using namespace spmc;
namespace bi = boost::interprocess;

std::atomic<bool> g_stop = { false };

void server (const std::string& name,
             int                numClients,
             size_t             messageSize,
             size_t             queueSize,
             uint32_t           rate)
{
  // define the number of clients to service
  SharedMemoryCounter clientCount (name + ":client:count", name);

  clientCount.set (numClients);

  // TODO a single sink could encapsulate sending to all streams

  // initialise the sinks for sending
  std::vector<std::unique_ptr<SPSCSink>> sinks;

  for (int i = 1; i < numClients+1; ++i)
  {
    std::string objectName = name + ":sink:" + std::to_string (i);

    auto sink = std::make_unique<SPSCSink> (name, objectName, queueSize);

    sinks.push_back (std::move (sink));
  }

  spmc::SignalCatcher s({ SIGINT, SIGTERM }, [&sinks] (int) {

    logger ().notice () << "stopping server..";

    g_stop = true;

    for (auto &sink : sinks)
    {
      logger ().info () << "stop " << sink->name ();
      sink->stop ();
    }
  });

  // wait for clients to be ready
  SharedMemoryCounter clientsReady (name + ":client:ready", name);

  logger ().info () << "waiting for " << numClients << " clients..";

  int toConnect = numClients;

  while (clientsReady.get () < numClients && !g_stop)
  {
    sleep_for (milliseconds (1));

    if ((numClients - clientsReady.get ()) != toConnect)
    {
      toConnect = numClients - clientsReady.get ();

      if (toConnect > 0)
      {
        logger ().info () << "waiting for " << toConnect << " clients..";
      }
    }
  }

  if (g_stop)
  {
    return;
  }

  logger ().info () << "start sending data";

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

int main(int argc, char *argv[])
{
  // set some defaults
  std::string level ("NOTICE");
  int clients        = 1;
  size_t messageSize = 1024;
  size_t queueSize   = 1024*1024*1024;
  uint32_t rate      = 0;
  int cpu            = -1;

  std::string name;

  ArgParse ()
    .optional ("--clients <number>",     "number of clients", clients)
    .optional ("--message-size <bytes>", "message size",      messageSize)
    .optional ("--queue-size <bytes>",   "queue size",        queueSize)
    .required ("--name <name>",          "memory name",       name)
    .optional ("--rate <msgs/sec>",
               "send data rate message /second "
               "zero indicates no throttling",        rate)
    .optional ("--loglevel <level>", "logging level", level)
    .choices  (Logger::level_strings ())
    .optional ("--cpu-bind <cpu>",
               "bind main thread to a cpu processor",  cpu)
   .run (argc, argv);

  logger ().info () << "start shared memory SPSC server";
  logger ().info () << "clients\t= "      << clients;
  logger ().info () << "message size\t= " << messageSize;
  logger ().info () << "message rate\t= "
                         << ((rate == 0) ? "max" : std::to_string (rate));
  logger ().info () << "queue size\t= "   << queueSize;
  logger ().info () << "memory name\t= "  << name;

  bind_to_cpu (cpu);

  /*
   * Create enough shared memory for each of the clients queues to fit plus some
   * extra room for shared memory booking.
   */
  auto memory =
    bi::managed_shared_memory (bi::open_or_create, name.c_str(),
                              (queueSize*clients) +
                              (SharedMemory::BOOK_KEEPING*clients));

  server (name, clients, messageSize, queueSize, rate);

  ASSERT_SS (bi::shared_memory_object::remove (name.c_str()),
             "Failed to remove shared memory object: " << name);

  logger ().info () << "exit spsc_server";

  return 1;
}
