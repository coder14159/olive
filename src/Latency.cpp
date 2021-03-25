#include "LatencyStats.h"

#include "Assert.h"
#include "Logger.h"


#include <boost/cstdint.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include <algorithm>
#include <sstream>

namespace ba = boost::accumulators;
namespace fs = boost::filesystem;

namespace spmc {

std::string nanoseconds_to_pretty (Nanoseconds::rep count)
{
  if (count == Nanoseconds::max ().count ())
  {
    return "-";
  }
  else if (count == Nanoseconds::min ().count ())
  {
    return "-";
  }
  else if (count < 1e3)
  {
    return (boost::str (boost::format ("%3d ns") % count));
  }
  else if (count < 1e6)
  {
    double usecs = static_cast<double> (count) / 1.0e3;
    return (boost::str (boost::format ("%3.0f us") % usecs));
  }
  else if (count < 1e9)
  {
    double msecs = static_cast<double> (count) / 1.0e6;
    return (boost::str (boost::format ("%3.0f ms") % msecs));
  }
  else if (count < (1e9*60))
  {
    double secs = static_cast<double> (count) / 1.0e9;
    return (boost::str (boost::format ("%3.0f s") % secs));
  }
  else
  {
    double mins = static_cast<double> (count) / (1e9*60);
    return (boost::str (boost::format ("%3.0f min") % mins));
  }
}

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds)
{
  return nanoseconds_to_pretty (nanoseconds.count ());
}

namespace {
std::map<float, Latency::Quantile> empty_quantiles ()
{
  std::map<float, Latency::Quantile> quantiles;

  quantiles[1]     = Latency::Quantile (ba::quantile_probability = 0.01);
  quantiles[5]     = Latency::Quantile (ba::quantile_probability = 0.05);
  quantiles[25]    = Latency::Quantile (ba::quantile_probability = 0.25);
  quantiles[50]    = Latency::Quantile (ba::quantile_probability = 0.50);
  quantiles[75]    = Latency::Quantile (ba::quantile_probability = 0.70);
  quantiles[80]    = Latency::Quantile (ba::quantile_probability = 0.80);
  quantiles[90]    = Latency::Quantile (ba::quantile_probability = 0.90);
  quantiles[95]    = Latency::Quantile (ba::quantile_probability = 0.95);
  quantiles[99]    = Latency::Quantile (ba::quantile_probability = 0.99);
  quantiles[99.5]  = Latency::Quantile (ba::quantile_probability = 0.995);
  quantiles[99.6]  = Latency::Quantile (ba::quantile_probability = 0.996);
  quantiles[99.7]  = Latency::Quantile (ba::quantile_probability = 0.997);
  quantiles[99.8]  = Latency::Quantile (ba::quantile_probability = 0.998);
  quantiles[99.9]  = Latency::Quantile (ba::quantile_probability = 0.999);
  quantiles[99.95] = Latency::Quantile (ba::quantile_probability = 0.9995);
  quantiles[99.99] = Latency::Quantile (ba::quantile_probability = 0.9999);

  return quantiles;
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
    ASSERT_SS (fs::create_directories (directory),
               "Failed to create directory: " << directory);

    BOOST_LOG_TRIVIAL (info) << "Created directory: " << directory;
  }

  ASSERT_SS (!filename.empty (), "Empty latency filename");

  fs::path file_path = fs::path (directory) / fs::path (filename);

  bool output_header = !fs::exists (file_path.string ());

  m_file.open (file_path.string (), std::ios::app|std::ios_base::out);

  ASSERT_SS (m_file.is_open (), "Failed to open file: " + file_path.string ());

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
         nanoseconds_to_pretty (max ());
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

} // namespace spmc
