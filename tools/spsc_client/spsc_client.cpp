#include "Assert.h"
#include "CpuBind.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SignalCatcher.h"
#include "SPSCStream.h"
#include "detail/CXXOptsHelper.h"
#include "detail/SharedMemory.h"
#include "detail/Utils.h"

#include <boost/log/trivial.hpp>

#include <thread>

using namespace spmc;

namespace bi = boost::interprocess;

namespace {

CxxOptsHelper parse (int argc, char* argv[])
{
  cxxopts::Options cxxopts ("spsc_server",
        "Message consumer for shared memory performance testing");

  std::vector<std::string> stats;

  cxxopts.add_options ()
    ("h,help", "Message consumer for SPSC shared memory performance testing")
    ("name", "Shared memory name", cxxopts::value<std::string> ())
    ("cpu", "bind main thread to a cpu processor id",
     cxxopts::value<int> ()->default_value ("-1"))
    // ("prefetchcache", "Size of a prefetch cache",
    //   cxxopts::value<size_t> ()->default_value ("0"))
    ("directory", "Directory for statistics files",
      cxxopts::value<std::string> ())
    ("stats", "Statistics to log. "
     "Comma separated list (throughput,latency,interval)",
      cxxopts::value<std::vector<std::string>> (stats))
    ("test", "Enable basic tests for message validity",
      cxxopts::value<bool> ())
    ("log_level", "l,Logging level",
      cxxopts::value<std::string> ()->default_value ("NOTICE"));

  CxxOptsHelper options (cxxopts.parse (argc, argv));

  options.positional ("stats", stats);

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

  auto name            = options.required<std::string> ("name");
  auto directory       = options.value<std::string>    ("directory", "");
  auto cpu             = options.value<int>            ("cpu", -1);
  auto test            = options.value<bool>           ("test", false);
  auto logLevel        = options.value<std::string>    ("log_level",
                                                        log_levels (),"INFO");
  auto latency         = options.positional ("stats", "latency");
  auto throughput      = options.positional ("stats", "throughput");
  auto interval        = options.positional ("stats", "interval");

  /*
   * TODO add a message drop option similar to spmc_client tool
   *
   * auto allowDrops   = options.value<bool> ("allowdrops", false);
   */

  BOOST_LOG_TRIVIAL (info) << "Start shared memory spsc_client";

  BOOST_LOG_TRIVIAL (info) << "Consume from: " << name;

  if (cpu != -1)
  {
    BOOST_LOG_TRIVIAL (info) << "Bind to CPU: " << cpu;
  }

  SPSCStream stream (name);

  bool stop = { false };

  /*
   * Handle signals
   */
  SignalCatcher s ({SIGINT, SIGTERM}, [&stream, &stop] (int) {

    if (!stop)
    {
      stop = true;

      stream.stop ();

      std::cout << "Stopping spsc_client" << std::endl;
    }
  });

  PerformanceStats stats (directory);

  stats.latency ().summary ().enable (latency);
  stats.latency ().interval ().enable (latency && interval);

  stats.throughput ().summary ().enable (throughput);
  stats.throughput ().interval ().enable (throughput && interval);

  bind_to_cpu (cpu);

  Header header;

  std::vector<uint8_t> data;
  std::vector<uint8_t> expected;

  while (SPMC_EXPECT_FALSE (!stop))
  {
    if (stream.next (header, data))
    {
      if (header.type == WARMUP_MESSAGE_TYPE)
      {
        continue;
      }

      stats.update (sizeof (Header) + data.size (), header.seqNum,
                    timepoint_from_nanoseconds_since_epoch (header.timestamp));
      if (test)
      {
        ASSERT_SS (header.size == data.size (), "Unexpected payload size: "
                  << data.size () << " expected: " << header.size);
        /*
         * Initialise the expected packet on receipt of the first message
         */
        if (expected.size () != data.size ())
        {
          expected.resize (data.size ());

          std::iota (std::begin (expected), std::end (expected), 1);
        }

        ASSERT_SS (expected.size () == data.size (),
                  "expected.size ()=" << expected.size ()
                  << " data.size ()=" << data.size ());

        ASSERT (expected == data, "Unexpected data packet payload");

        data.clear ();
      }
    }
  }

  BOOST_LOG_TRIVIAL (info) << "Exit stream";

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
