#define SPMC_ENABLE_ASSERTS 1

#include "Assert.h"
#include "CpuBind.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SPMCStream.h"
#include "detail/SharedMemory.h"
#include "detail/CXXOptsHelper.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/log/trivial.hpp>

#include <exception>

#include <cxxopts.hpp>

using namespace spmc;

namespace bi = boost::interprocess;

std::atomic<bool> g_stop = { false };

namespace {

CxxOptsHelper parse (int argc, char* argv[])
{
  cxxopts::Options cxxopts ("spmc_client",
                        "Consume messages sent through shared memory");

  cxxopts.add_options ()
    ("h,help", "Performance test consuming of shared memory messages")
    ("name", "Shared memory name",
      cxxopts::value<std::string> ())
    ("allowdrops", "Allow message drops")
    ("cpubind", "Bind main thread to a cpu processor integer (default off)",
      cxxopts::value<size_t> ()->default_value ("0"))
    ("p,prefetchcache", "Size of a prefetch cache",
      cxxopts::value<size_t> ()->default_value ("0"))
    ("directory", "Directory for statistics files",
      cxxopts::value<std::string> ())
    ("intervalstats", "Periodically print statistics ",
      cxxopts::value<bool> ())
    ("latencystats", "Enable latency statistics",
      cxxopts::value<bool> ())
    ("throughputstats", "Enable throughput statistics",
      cxxopts::value<bool> ())
    ("test", "Enable basic tests for message validity",
      cxxopts::value<bool> ())
    ("loglevel", "l,Logging level",
      cxxopts::value<std::string> ()->default_value ("NOTICE"));

  CxxOptsHelper options (cxxopts.parse (argc, argv));

  if (options.exists ("help"))
  {
    std::cout << cxxopts.help ({"", "Group"}) << std::endl;
    exit (0);
  }

  return options;
}

} // namespace {

int main(int argc, char* argv[]) try
{
  auto options = parse (argc, argv);

  std::vector<std::string> logLevels = { "TRACE", "DEBUG", "INFO",
                                         "WARNING", "ERROR", "FATAL" };

  /*
   * Get required and default values
   */
  auto name            = options.required<std::string> ("name");
  auto cpubind         = options.value<size_t> ("cpubind", -1);
  auto allowDrops      = options.value<bool>   ("allowdrops", false);
  auto intervalStats   = options.value<bool>   ("intervalstats", false);
  auto throughputStats = options.value<bool>   ("throughputstats", false);
  auto latencyStats    = options.value<bool>   ("latencystats", false);
  auto prefetchCache   = options.value<size_t> ("prefetchcache", 0);
  auto test            = options.value<bool>   ("test", false);
  auto logLevel = options.value<std::string>   ("loglevel", logLevels, "INFO");

#if 1
  std::cout << "name: "            << name << '\n'
            << "loglevel: "        << logLevel << '\n'
            << "cpu: "             << cpubind << '\n'
            << "prefetchCache: "   << prefetchCache << '\n'
            << "allowDrops: "      << std::boolalpha << allowDrops << '\n'
            << "intervalStats: "   << std::boolalpha << intervalStats << '\n'
            << "throughputStats: " << std::boolalpha << throughputStats << '\n'
            << "latencyStats: "    << std::boolalpha << latencyStats << '\n'
            << "test: "            << std::boolalpha << test
            << std::endl;
#endif

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) <<  "Start shared memory spmc_client";
  BOOST_LOG_TRIVIAL (info) <<  "Consume from: " << name;

  using Queue  = SPMCQueue<SharedMemory::Allocator>;
  using Stream = SPMCStream<Queue>;

  Stream stream (name, name + ":queue", allowDrops, prefetchCache);

  return 0;

#if 0

  auto result = options.parse(argc, argv);

  ArgParse ().required ("--name <name>",
                        "shared memory name to consume data", name)
             .optional ("--allow-drops",
                        "allow message drops", allowMessageDrops)
             .optional ("--prefetch-cache <size>",
                        "size of a prefetch cache", prefetchCache)
             .optional ("--directory <path>",
                        "directory for statistics files", directory)
             .optional ("--interval-stats", "periodically print statistics",
                        intervalStats)
             .optional ("--latency-stats", "enable latency statistics",
                        latencyStats)
             .optional ("--throughput-stats", "enable throughput statistics",
                        throughputStats)
             .optional ("--test",
                        "basic tests for message validity", test)
             .optional ("--loglevel <level>", "logging level", level)
             .choices  (Logger::level_strings ())
             .optional ("--cpu-bind <cpu>",
                        "bind main thread to a cpu processor",  cpu)

             .description ("consume messages sent through shared memory")
             .run (argc, argv);

  logger ().set_level (level);

  logger ().info () << "Start shared memory spmc_client";

  logger ().info () << "Consume from: " << name;

  using Queue  = SPMCQueue<SharedMemory::Allocator>;
  using Stream = SPMCStream<Queue>;

  SignalCatcher signals({ SIGINT, SIGTERM });

  Stream stream (name, name + ":queue", allowMessageDrops, prefetchCache);

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
        assert(header.size == data.size ());

        /*
         * Initialise the expected packet on receipt of the first message
         */
        if (expected.size () < data.size ())
        {
          expected.resize (data.size ());
          std::iota (std::begin (expected), std::end (expected), 1);
        }

        ASSERT_SS(expected.size () == data.size (),
                "expected.size ()=" << expected.size ()
                << " data.size ()=" << data.size ());
        ASSERT(expected == data);

        data.clear ();
      }
    }
  }

  logger ().notice () << "exit shared memory spmc_client";
#endif
  return 1;
}
// catch (const cxxopts::OptionSpecException &e)
// {
//   std::cerr << "Error1a: " << e.what () << std::endl;
// }
// catch (const cxxopts::OptionParseException &e)
// {
//   std::cerr << "Error1: " << e.what () << std::endl;
// }
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
catch (const std::exception &e)
{
  std::cerr << "Error3: " << e.what () << std::endl;
}