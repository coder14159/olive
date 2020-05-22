#include "CpuBind.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SPSCStream.h"
#include "detail/SharedMemory.h"

#include "patch/lockfree/spsc_queue.hpp"

#include "spmc_argparse.h"
#include "spmc_signal_catcher.h"
#include "spmc_time_duration.h"

#include <boost/interprocess/managed_shared_memory.hpp>

#include <thread>

using namespace spmc;

namespace bi = boost::interprocess;

std::atomic<bool> g_stop = { false };

int main(int argc, char* argv[])
{
  std::string name;
  std::string directory;
  std::string level ("NOTICE");

  size_t prefetchCache  = 0;
  bool test             = false;
  bool intervalStats    = false;
  bool latencyStats     = false;
  bool throughputStats  = false;
  size_t cpu            = -1;

  ArgParse ().required ("--name <name>",
                        "shared memory name to consume data", name)
             .optional ("--directory <path>",
                        "directory for statistics files", directory)
             .optional ("--prefetch-cache <size>",
                        "size of a prefetch cache", prefetchCache)
             .optional ("--interval-stats", "periodically print statistics",
                        intervalStats)
             .optional ("--latency-stats", "enable latency statistics",
                        latencyStats)
             .optional ("--throughput-stats", "enable throughput statistics",
                        throughputStats)
             .optional ("--test", "basic tests for messege validity", test)
             .optional ("--loglevel <level>", "logging level", level)
             .choices (Logger::level_strings ())
             .optional ("--cpu-bind <cpu>",
                        "bind main thread to a cpu processor", cpu)
             .description ("consume messages through shared memory")
             .run (argc, argv);

  logger ().info () << "Start shared memory spsc_client";

  logger ().info () << "Consume from: " << name;

  SignalCatcher signals({ SIGINT, SIGTERM });

  SPSCStream stream (name, prefetchCache);

  PerformanceStats stats (directory);

  stats.throughput ().summary () .enable (throughputStats);
  stats.throughput ().interval ().enable (throughputStats && intervalStats);

  stats.latency ().summary () .enable (latencyStats);
  stats.latency ().interval ().enable (latencyStats && intervalStats);

  signals.reset ([&] (int) {
    logger ().notice () << "stopping client..";
    g_stop = true;
    stream.stop ();
  });

  bind_to_cpu (cpu);

  ipc::Header header;

  std::vector<uint8_t> data;
  std::vector<uint8_t> expected;

  logger ().notice () << "sizeof Header=" << sizeof (Header);



  while (true)
  {
    if (g_stop)
    {
      logger ().notice () << "stopping stream..";
      break;
    }

    if (stream.next (header, data))
    {
      stats.update (sizeof (Header), data.size (), header.seqNum,
                    Time::deserialise (header.timestamp));
      if (test)
      {
        assert (header.size == data.size ());

        if (expected.size () < data.size ())
        {
          expected.resize (data.size ());

          std::iota (std::begin (expected), std::end (expected), 1);
        }

        ASSERT_SS (expected.size () == data.size (),
                "expected.size ()=" << expected.size ()
                << " data.size ()=" << data.size ());
        assert (expected == data);

        data.clear ();
      }
    }
  }

  logger ().info () << "exit shared memory spsc_client";

  return 1;
}
