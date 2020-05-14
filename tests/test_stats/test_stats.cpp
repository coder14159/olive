#include "src/PerformanceStats.h"
#include "src/detail/SharedMemory.h"

// #include "spmc_filesystem.h"
// #include "spmc_time.h"
// #include "spmc_time_duration.h"
// #include "spmc_timer.h"

#include <boost/scope_exit.hpp>

#define BOOST_TEST_DYN_LINK

#define BOOST_TEST_MODULE StatsTests
#include <boost/test/unit_test.hpp>

using namespace spmc;

BOOST_AUTO_TEST_CASE (ThroughputStatsUpdates)
{
	logger ().set_level (WARNING);

	std::vector<int> payload;
	payload.resize (10240);

	PerformanceStats stats;

	stats.throughput ().summary ().enable (true);

	auto start = Time::now ();

	for (int i = 1; ; ++i)
	{
	stats.update (sizeof (ipc::Header), payload.size (), i, Time::now ());

	if ((Time::now () - start) > seconds (2))
	{
	break;
	}
	}

	BOOST_CHECK (stats.throughput ().summary ()
	.megabytes_per_sec (Time::now ()) > 100);

}

BOOST_AUTO_TEST_CASE (LatencyStatsUpdateIsFast)
{
  auto level = ipc::logger ().level ();
  ipc::logger ().set_level (ERROR);

  BOOST_SCOPE_EXIT (&level) {
    ipc::logger ().set_level (level);
  }BOOST_SCOPE_EXIT_END;

	std::vector<int> payload;
	payload.resize (128);

  TempDir dir;

	PerformanceStats stats (dir.path ());

	stats.latency ().summary ().enable (true);

	auto start = Time::now ();

	for (int i = 1; ; ++i)
	{
	stats.update (sizeof (ipc::Header), payload.size (), i, Time::now ());

	if ((Time::now () - start) > seconds (2))
	{
	break;
	}
	}

 	auto &quantiles = stats.latency ().summary ().quantiles ();
	BOOST_CHECK (quantiles.empty () == false);

	auto median =
	static_cast<int64_t>(boost::accumulators
	 	::p_square_quantile (quantiles.at (50)));

	BOOST_CHECK (nanoseconds (median)  < microseconds (2));

	for (auto &s : stats.latency ().summary ().to_strings ())
	{
	BOOST_TEST_MESSAGE (s);
	}
}
