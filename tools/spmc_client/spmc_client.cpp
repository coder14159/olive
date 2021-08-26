#include "Assert.h"
#include "CpuBind.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SignalCatcher.h"
#include "SPMCStream.h"
#include "detail/CXXOptsHelper.h"
#include "detail/SharedMemory.h"
#include "detail/Utils.h"

#include <bits/stdc++.h>
#include <exception>
#include <set>

using namespace spmc;

namespace bi = boost::interprocess;

namespace {

bool validate_options (const std::vector<std::string> &options,
                       const std::vector<std::string> &valid_options)
{
  for (auto option : options)
  {
    if (std::find (valid_options.begin (), valid_options.end (),
                         option) == valid_options.end ())
    {
      std::cerr << "Invalid option: " << option << std::endl;

      return false;
    }
  }

  return true;
}

CxxOptsHelper parse (int argc, char* argv[])
{
  cxxopts::Options cxxopts ("spmc_client",
                "Consume messages sent to local named shared memory");

  std::vector<std::string> stats;
  std::vector<std::string> valid_stats { "throughput", "latency", "interval" };

  cxxopts.add_options ()
    ("h,help", "Performance test consuming of shared memory messages")
    ("name", "Shared memory name", cxxopts::value<std::string> ())
    ("allow_drops", "Allow message drops")
    ("cpu", "Bind main thread to a cpu processor integer, "
            "use -1 for no binding",
      cxxopts::value<int> ()->default_value ("-1"))
    ("directory", "Directory for statistics files",
      cxxopts::value<std::string> ())
    ("stats", "Statistics to log. "
     "Comma separated list (throughput,latency,interval)",
      cxxopts::value<std::vector<std::string>> (stats))
    ("test", "Enable basic tests for message validity", cxxopts::value<bool> ())
    ("log_level", "Logging level",
      cxxopts::value<std::string> ()->default_value ("NOTICE"));

  CxxOptsHelper options (cxxopts.parse (argc, argv));

  if (options.exists ("help"))
  {
    std::cout << cxxopts.help ({"", "Group"}) << std::endl;

    exit (EXIT_SUCCESS);
  }

  options.positional ("stats", stats);

  if (!validate_options (stats, valid_stats))
  {
    exit (EXIT_FAILURE);
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

  /*
   * Get required and default values
   */
  auto name       = options.required<std::string> ("name");
  auto directory  = options.value<std::string>    ("directory", "");
  auto cpu        = options.value<int>            ("cpu", -1);
  auto test       = options.value<bool>           ("test", false);
  auto logLevel   = options.value<std::string>    ("log_level",
                                                      log_levels (),"INFO");
  auto latency    = options.positional ("stats", "latency");
  auto throughput = options.positional ("stats", "throughput");
  auto interval   = options.positional ("stats", "interval");

  set_log_level (logLevel);

  BOOST_LOG_TRIVIAL (info) << "Start spmc_client";
  BOOST_LOG_TRIVIAL (info) << "Consume from shared memory named: " << name;

  if (cpu != -1)
  {
    BOOST_LOG_TRIVIAL (info) << "Bind to CPU: " << cpu;
  }

  using Queue  = SPMCQueue<SharedMemory::Allocator>;
  using Stream = SPMCStream<Queue>;

  // TODO: Generate the queue name from within Stream ctor..
  Stream stream (name, name + ":queue");

  std::atomic<bool> stop = { false };
  /*
   * Handle signals
   */
  SignalCatcher s ({SIGINT, SIGTERM}, [&stream, &stop] (int) {

    if (!stop)
    {
      BOOST_LOG_TRIVIAL (debug) << "Stop spmc_client";

      stop = true;

      stream.stop ();
    }
  });

  TimeDuration warmup (Seconds (2));
  PerformanceStats stats (directory, warmup);

  stats.latency ().summary ().enable (latency);
  stats.latency ().interval ().enable (interval && latency);

  stats.throughput ().summary ().enable (throughput);
  stats.throughput ().interval ().enable (interval && throughput);

  bind_to_cpu (cpu);

  Header header;
  uint64_t testSeqNum = 0;

  std::vector<uint8_t> data;
  std::vector<uint8_t> expected;

  while (!stop)
  {
    if (stream.next (header, data))
    {
      stats.update (sizeof (Header) + header.size, header.seqNum,
                    timepoint_from_nanoseconds_since_epoch (header.timestamp));

      if (SPMC_EXPECT_FALSE (test))
      {
        if (testSeqNum == 0)
        {
          testSeqNum = header.seqNum;
        }
        else
        {
          CHECK_SS ((header.seqNum - testSeqNum) == 1,
            "Invalid sequence number: header.seqNum: " << header.seqNum <<
            " testSeqNum: " << testSeqNum);

          testSeqNum = header.seqNum;
        }

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
      else
      {
        std::vector<uint8_t> a (data);
        (void)a;
      }
    }
  }

  stats.stop ();
  stats.print_summary ();

  BOOST_LOG_TRIVIAL (info) << "Exit SPMCStream";

  return EXIT_SUCCESS;
}
catch (const cxxopts::OptionException &e)
{
  std::cerr << e.what () << std::endl;
}
catch (const std::exception &e)
{
  std::cerr << e.what () << std::endl;
}