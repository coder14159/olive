#include "CpuBind.h"
#include "Logger.h"
#include "SPMCQueue.h"
#include "SPMCSink.h"
#include "Throttle.h"

#include "spmc_argparse.h"
#include "spmc_signal_catcher.h"

#include <boost/interprocess/managed_shared_memory.hpp>

#include <atomic>

using namespace spmc;
using namespace spmc;
namespace bi = boost::interprocess;

std::atomic<bool> g_stop = { false };

void server (const std::string& name,
             size_t             messageSize,
             size_t             queueSize,
             uint32_t           rate)
{
  using Queue = SPMCQueue<SharedMemory::Allocator>;
  using Sink  = SPMCSink<Queue>;

  Sink sink (name, name + ":queue", queueSize);

  spmc::SignalCatcher s({ SIGINT, SIGTERM }, [&sink] (int) {
    logger ().notice () << "stopping server..";
    g_stop = true;
    sink.stop ();
  });

  logger ().info () << "start sending data";

  // create a reusable test message
  std::vector<uint8_t> message (messageSize, 0);

  std::iota (std::begin (message), std::end (message), 1);

  Throttle throttle (rate);

  while (!g_stop)
  {
    throttle.throttle ();

    sink.next (message);
  }
}

int main(int argc, char *argv[])
{
  // set some defaults
  std::string level ("NOTICE");
  size_t messageSize = 1024;
  size_t queueSize   = 1024*1024*1024;
  uint32_t rate      = 0;
  int cpu            = -1;

  std::string name;

  ArgParse ()
    .optional ("--message-size <bytes>", "message size", messageSize)
    .optional ("--queue-size <bytes>",   "queue size",   queueSize)
    .optional ("--name <name>",          "memory name",  name)
    .optional ("--rate <msgs/sec>",
               "message throughput, zero indicates no throttling", rate)
    .optional ("--loglevel <level>", "logging level", level)
    .choices  (Logger::level_strings ())
    .optional ("--cpu-bind <cpu>",
               "bind main thread to a cpu processor",  cpu)
    .run (argc, argv);

  logger ().info () << "start shared memory SPMC server";
  logger ().info () << "message size\t= " << messageSize;
  logger ().info () << "message rate\t= "
                         << ((rate == 0) ? "max" : std::to_string (rate));
  logger ().info () << "queue size\t= "   << queueSize;
  logger ().info () << "memory name\t= "  << name;

  bind_to_cpu (cpu);

  /*
   * Create enough shared memory for a single queue which is shared by all the
   * clients.
   */
  auto memory =
    bi::managed_shared_memory (bi::open_or_create, name.c_str(),
                              (queueSize) +
                              (SharedMemory::BOOK_KEEPING));

  server (name, messageSize, queueSize, rate);

  ipc::logger ().info () << "exit spmc_server";

  return 1;
}
