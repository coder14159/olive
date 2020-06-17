#include "Assert.h"
#include "CpuBind.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SignalCatcher.h"
#include "SPSCStream.h"
#include "detail/CXXOptsHelper.h"
#include "detail/SharedMemory.h"

#include <boost/log/trivial.hpp>

#include <thread>

using namespace spmc;

namespace bi = boost::interprocess;

namespace {

std::atomic<bool> g_stop = { false };

CxxOptsHelper parse (int argc, char* argv[])
{
  std::string oneKB = std::to_string (1024);
  std::string oneGB = std::to_string (1024*1024*1024);
  // std::string level = "NOTICE";
  // std::string rate  = "0";
  // std::string cpu   = "-1";

  cxxopts::Options cxxopts ("spsc_server",
        "Message consumer for shared memory performance testing");

  cxxopts.add_options ()
    ("h,help", "Message consumer for SPSC shared memory performance testing")
    ("name", "Shared memory name", cxxopts::value<std::string> ())
    ("cpu", "bind main thread to a cpu processor id",
     cxxopts::value<int> ()->default_value ("-1"))
    ("prefetchcache", "Size of a prefetch cache",
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

  auto name            = options.required<std::string> ("name");
  auto directory       = options.value<std::string> ("directory", "");
  auto cpu             = options.value<int> ("cpu", -1);
  auto prefetchCache   = options.value<size_t> ("prefetchcache", 0);
  auto intervalStats   = options.value<bool>   ("intervalstats", false);
  auto latencyStats    = options.value<bool>   ("latencystats", false);
  auto throughputStats = options.value<bool>   ("throughputstats", false);
  auto test            = options.value<bool>   ("test", false);
  auto logLevel = options.value<std::string> ("loglevel", log_levels (),"INFO");

  // TODO add message drop option
//  auto allowDrops      = options.value<bool>   ("allowdrops", false);

  BOOST_LOG_TRIVIAL (info) << "Start shared memory spsc_client";

  BOOST_LOG_TRIVIAL (info) << "Consume from: " << name;
  BOOST_LOG_TRIVIAL (info) << "CPU: " << cpu;

  SPSCStream stream (name, prefetchCache);

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
        assert (header.size == data.size ());

        /*
         * Initialise the expected packet on receipt of the first message
         */
        if (expected.size () < data.size ())
        {
          expected.resize (data.size ());

          std::iota (std::begin (expected), std::end (expected), 1);
        }

        ASSERT_SS (expected.size () == data.size (),
                "expected.size ()=" << expected.size ()
                << " data.size ()=" << data.size ());

        ASSERT (expected == data, "unexpected data packet");

        data.clear ();
      }
    }
  }

  BOOST_LOG_TRIVIAL (info) << "exit shared memory spsc_client";

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
