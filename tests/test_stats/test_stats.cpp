#include "Logger.h"
#include "PerformanceStats.h"
#include "detail/SharedMemory.h"

#include "boost/filesystem.hpp"

#define BOOST_TEST_DYN_LINK

#define BOOST_TEST_MODULE StatsTests
#include <boost/test/unit_test.hpp>

using namespace olive;

using namespace boost::log::trivial;

BOOST_AUTO_TEST_CASE(ThroughputStatsUpdates)
{
  olive::ScopedLogLevel log (warning);

  size_t payload_size = 10240;

  PerformanceStats stats;

  auto start = Clock::now ();

  for (int i = 1;; ++i)
  {
    stats.update (payload_size, i, Clock::now ());

    if ((Clock::now() - start) > Seconds (2))
    {
      break;
    }
  }

  BOOST_CHECK (stats.throughput ().summary ()
                    .megabytes_per_sec () > 100);
}

BOOST_AUTO_TEST_CASE (LatencyStatsUpdateIsFast)
{
  olive::ScopedLogLevel log (warning);

  namespace fs = boost::filesystem;

  struct TempDir
  {
    TempDir ()
    {
      m_path  = fs::temp_directory_path ()
              / fs::unique_path ("%%%%-%%%%-%%%%-%%%%");

      fs::create_directories (m_path);
    }

    ~TempDir () { fs::remove_all (m_path); }

    const std::string path () const { return m_path.native (); }

  private:

    fs::path m_path;
  };

  uint8_t payload_size = 32;

  TempDir dir;

  PerformanceStats stats (dir.path ());

  auto start = Clock::now ();

  for (int i = 1;; ++i)
  {
    stats.update (payload_size, i, Clock::now ());

    if ((Clock::now () - start) > Seconds (2))
    {
      break;
    }
  }

  auto &quantiles = stats.latency ().summary ().quantiles ();

  BOOST_CHECK (quantiles.empty () == false);

  auto median = static_cast<int64_t> (
                  boost::accumulators::p_square_quantile (quantiles.at (50)));

  BOOST_CHECK (Nanoseconds (median) < Microseconds (2));

  for (auto &s : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (s);
  }

  BOOST_CHECK (fs::exists (dir.path ()));
  BOOST_CHECK (fs::exists (dir.path () / fs::path ("latency-interval.csv")));
  BOOST_CHECK (fs::exists (dir.path () / fs::path ("latency-summary.csv")));
}
