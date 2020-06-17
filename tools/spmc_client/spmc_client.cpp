#define SPMC_ENABLE_ASSERTS 1

#include "Assert.h"
#include "CpuBind.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SignalCatcher.h"
#include "SPMCStream.h"
#include "detail/CXXOptsHelper.h"
#include "detail/SharedMemory.h"

#include <boost/log/trivial.hpp>

#include <exception>

using namespace spmc;

namespace bi = boost::interprocess;

namespace {

std::atomic<bool> g_stop = { false };

CxxOptsHelper parse (int argc, char* argv[])
{
  cxxopts::Options cxxopts ("spmc_client",
                "Consume messages sent to local named shared memory");

  cxxopts.add_options ()
    ("h,help", "Performance test consuming of shared memory messages")
    ("name", "Shared memory name", cxxopts::value<std::string> ())
    ("allowdrops", "Allow message drops")
    ("cpu", "Bind main thread to a cpu processor integer (default off)",
      cxxopts::value<int> ()->default_value ("-1"))
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

    exit (EXIT_SUCCESS);
  }

  return options;
}

} // namespace {

int main(int argc, char* argv[]) try
{
  auto options = parse (argc, argv);

  /*
   * Get required and default values
   */
  auto name            = options.required<std::string> ("name");
  auto directory       = options.value<std::string> ("directory", "");
  auto cpu             = options.value<int>    ("cpu", -1);
  auto allowDrops      = options.value<bool>   ("allowdrops", false);
  auto intervalStats   = options.value<bool>   ("intervalstats", false);
  auto throughputStats = options.value<bool>   ("throughputstats", false);
  auto latencyStats    = options.value<bool>   ("latencystats", false);
  auto prefetchCache   = options.value<size_t> ("prefetchcache", 0);
  auto test            = options.value<bool>   ("test", false);
  auto logLevel = options.value<std::string> ("loglevel", log_levels (),"INFO");

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) <<  "Start spmc_client";
  BOOST_LOG_TRIVIAL (info) <<  "Consume from shared memory named: " << name;

  using Queue  = SPMCQueue<SharedMemory::Allocator>;
  using Stream = SPMCStream<Queue>;

  Stream stream (name, name + ":queue", allowDrops, prefetchCache);
  /*
   * Handle signals
   */
  SignalCatcher s ({SIGINT, SIGTERM}, [&stream] (int) {
    g_stop = true;

    std::cout << "stopping.." << std::endl;

    stream.stop ();
  });

  PerformanceStats stats (directory);

  stats.throughput ().summary () .enable (throughputStats);
  stats.throughput ().interval ().enable (throughputStats && intervalStats);

  stats.latency ().summary () .enable (latencyStats);
  stats.latency ().interval ().enable (latencyStats && intervalStats);

  bind_to_cpu (cpu);

  Header header;

  std::vector<uint8_t> data;
  std::vector<uint8_t> expected;

  while (true)
  {
    if (g_stop)
    {
      BOOST_LOG_TRIVIAL (info) << "stopping stream..";
      break;
    }

    if (stream.next (header, data))
    {
      stats.update (sizeof (Header), data.size (), header.seqNum,
                    timepoint_from_nanoseconds_since_epoch (header.timestamp));

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
        ASSERT(expected == data, "unexpected data packet");

        data.clear ();
      }
    }
  }

  BOOST_LOG_TRIVIAL (info) << "exit shared memory spmc_client";

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}

