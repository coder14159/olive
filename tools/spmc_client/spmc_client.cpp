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

CxxOptsHelper parse (int argc, char* argv[])
{
  cxxopts::Options cxxopts ("spmc_client",
                "Consume messages sent to local named shared memory");

  std::vector<std::string> stats;

  cxxopts.add_options ()
    ("h,help", "Performance test consuming of shared memory messages")
    ("name", "Shared memory name", cxxopts::value<std::string> ())
    ("allow_drops", "Allow message drops")
    ("cpu", "Bind main thread to a cpu processor integer, "
            "use -1 for no binding",
      cxxopts::value<int> ()->default_value ("-1"))
    ("directory", "Directory for statistics files",
      cxxopts::value<std::string> ())
    ("test", "Enable basic tests for message validity",
      cxxopts::value<bool> ())
    ("stats", "Statistics to log. "
     "Comma separated list (throughput,latency,interval)",
      cxxopts::value<std::vector<std::string>> (stats))
    ("log_level", "Logging level",
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

  bool stop = { false };
  /*
   * Handle signals
   */
  SignalCatcher s ({SIGINT, SIGTERM}, [&stream, &stop] (int) {

    if (!stop)
    {
      BOOST_LOG_TRIVIAL (info) << "Stopping spmc_client";

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
#define TEST1
// #define TEST1_SMALLVECTOR


#ifdef TEST1
        // 1.6 GB/s 26.4 M msgs/s
        auto a = data;
        a.clear ();
#endif

#ifdef TEST1_SMALLVECTOR
        // 1.6 GB/s 27.0 M msgs/s
        boost::container::small_vector<uint8_t, 64> a (data);
        a.clear ();
#endif

#ifdef TEST1_B
        // 1.6 GB/s 27.0 M msgs/s
        std::vector<uint8_t> a (data);
        a.clear ();
#endif

#ifdef TEST1_A
        // 1.5 GB/s 24.6 M msgs/s
        auto a = data;
        a.clear ();
#endif

#ifdef TEST2
        // 932 MB/s 15.3 M msgs/s
        data.clear ();
#endif
#ifdef TEST3
        // 924 MB/s 15.1 M msgs/s
        // uint8_t i = 0;
        for (uint8_t i : data)
        {
          i = 0;
          (void)i;
        }
        // (void)i;
        // data.clear ();
#endif
#ifdef TEST4
        // 1.3 GB/s 22.6 M msgs/s
        // uint8_t i = 0;
        for (uint8_t i : data)
        {
          i = 0;
          (void)i;
        }
#endif

#ifdef TEST5
        // 947 MB/s 15.5 M msgs/expected
        expected = data;
        expected.clear ();
#endif
#ifdef TEST6
        // 947 MB/s 15.5 M msgs/s
        expected = data;
#endif

        // ASSERT (header.size == data.size (), "header.size != data.size");
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