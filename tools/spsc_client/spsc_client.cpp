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

#include <bits/stdc++.h>
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
    ("prefetch_size", "Set size of a prefetch cache. "
                      "The default of 0 indicates no prefetching",
      cxxopts::value<size_t> ()->default_value ("0"))
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
  /*
   * Improve I/O latency by switching off synchronisation
   */
  std::ios_base::sync_with_stdio (false);

  auto options = parse (argc, argv);

  auto name          = options.required<std::string> ("name");
  auto directory     = options.value<std::string>    ("directory", "");
  auto cpu           = options.value<int>            ("cpu", -1);
  auto prefetchSize  = options.value<size_t>         ("prefetch_size", 0);
  auto test          = options.value<bool>           ("test", false);
  auto logLevel      = options.value<std::string>    ("log_level",
                                                      log_levels (),"INFO");
  auto latency       = options.positional ("stats", "latency");
  auto throughput    = options.positional ("stats", "throughput");
  auto interval      = options.positional ("stats", "interval");

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) << "Start spsc_client";
  BOOST_LOG_TRIVIAL (info) << "Consume from shared memory named: " << name;

  if (cpu != -1)
  {
    BOOST_LOG_TRIVIAL (info) << "Bind to CPU: " << cpu;
  }
  if (prefetchSize > 0)
  {
    BOOST_LOG_TRIVIAL (info) <<  "Use prefetch cache size: " << prefetchSize;
  }

  SPSCStreamProcess stream (name, prefetchSize);

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

  TimeDuration warmup (Seconds (2));
  PerformanceStats stats (directory, warmup);

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
      stats.update (sizeof (Header) + header.size, header.seqNum,
                    timepoint_from_nanoseconds_since_epoch (header.timestamp));
      if (test)
      {
        CHECK_SS (header.size == data.size (), "Unexpected payload size: "
                  << data.size () << " expected: " << header.size);
        /*
         * Initialise the expected packet on receipt of the first message
         */
        if (expected.size () != data.size ())
        {
          expected.resize (data.size ());

          std::iota (std::begin (expected), std::end (expected), 1);
        }

        CHECK_SS (expected.size () == data.size (),
                  "expected.size ()=" << expected.size ()
                  << " data.size ()=" << data.size ());

        CHECK (expected == data, "Unexpected data packet payload");

        data.clear ();
      }
    }
  }

  stats.stop ();
  stats.print_summary ();

  BOOST_LOG_TRIVIAL (info) << "Exit stream";

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
