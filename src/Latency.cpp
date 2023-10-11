#include "LatencyStats.h"

#include "Assert.h"
#include "Logger.h"


#include <boost/cstdint.hpp>
#include <boost/format.hpp>

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace ba = boost::accumulators;
namespace fs = std::filesystem;

namespace olive {

namespace {

std::map<float, Latency::Quantile> empty_quantiles ()
{
  std::map<float, Latency::Quantile> quantile_values;

  std::vector<float> quantiles = {
    1, 10, 25, 50, 75, 80, 90, 95, 99,
    99.5, 99.6, 99.7, 99.8, 99.9, 99.95, 99.99
  };

  std::for_each (quantiles.begin (), quantiles.end (),
                 [&quantile_values] (float q) {
    quantile_values[q] = Latency::Quantile (ba::quantile_probability = q/100.);
  });

  return quantile_values;
}

} // namespace {

Latency::Latency ()
: m_empty (empty_quantiles ())
, m_quantiles (empty_quantiles ())
{ }

Latency::Latency (const std::string &directory, const std::string &filename)
: m_empty (empty_quantiles ())
, m_quantiles (empty_quantiles ())
{
  if (directory.empty () || filename.empty ())
  {
    return;
  }

  if (!fs::exists (directory))
  {
    CHECK_SS (fs::create_directories (directory),
               "Failed to create directory: " << directory);

    BOOST_LOG_TRIVIAL (info) << "Created directory: " << directory;
  }

  CHECK_SS (!filename.empty (), "Empty latency filename");

  fs::path file_path = fs::path (directory) / fs::path (filename);

  bool output_header = !fs::exists (file_path.string ());

  m_file.open (file_path.string (), std::ios::app|std::ios_base::out);

  CHECK_SS (m_file.is_open (), "Failed to open file: " << file_path.string ());

  BOOST_LOG_TRIVIAL (info) << "Latency file: " << file_path.string ();

  if (output_header)
  {
    write_header ();
  }
}

Latency::~Latency ()
{
  stop ();
}

void Latency::enable (bool enable)
{
  if (!enable)
  {
    stop ();
  }
}

void Latency::stop ()
{
  m_stop = true;
}

void Latency::write_header ()
{
  if (!m_file.is_open ())
  {
    return;
  }

  boost::format fmt ("%d");

  m_file << "0";

  for (auto &quantile : m_quantiles)
  {
     m_file << "," << fmt % quantile.first;
  }

  m_file << ",100\n";
}

Latency &Latency::write_data ()
{
  if (m_stop || !m_file || !m_file.is_open ())
  {
    return *this;
  }

  m_file << m_min.count ();

  for (const auto &quantile : m_quantiles)
  {
    m_file << ","
           << static_cast<int64_t>(ba::p_square_quantile (quantile.second));
  }

  m_file << "," << m_max.count () << "\n";

  return *this;
}

std::string Latency::to_string () const
{
  auto median = static_cast<int64_t>(ba::p_square_quantile (m_quantiles.at (50)));

  return nanoseconds_to_pretty (min ()) + " " +
         nanoseconds_to_pretty (median) + " " +
         nanoseconds_to_pretty (max ()) + " ";
}

std::vector<std::string> Latency::to_strings () const
{
  if (m_stop)
  {
    return {};
  }

  std::vector<std::string> stats;

  stats.push_back (
      (boost::format ("%s %s") % "percentile" % "latency").str ());
  stats.push_back (
      (boost::format ("%s %s") % "----------" % "-------").str ());

  stats.push_back ((boost::format ("%-10s %7s")
                    % "min"
                    % nanoseconds_to_pretty (m_min)).str ());

  boost::format fmt ("%-10d %7s");

  for (const auto &quantile : m_quantiles)
  {
    int64_t t = ba::p_square_quantile (quantile.second);

    stats.push_back ((fmt % quantile.first
                          % nanoseconds_to_pretty (t)).str ());
  }

  stats.push_back ((boost::format ("%-10s %7s")
                    % "max"
                    % nanoseconds_to_pretty (m_max)).str ());

  return stats;
}

} // namespace olive
