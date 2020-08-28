#include "Assert.h"
#include "Logger.h"
#include "LatencyStats.h"

#include <boost/cstdint.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include <algorithm>
#include <sstream>

namespace ba = boost::accumulators;
namespace fs = boost::filesystem;

namespace spmc {

inline
std::string nanoseconds_to_pretty (Nanoseconds nanoseconds)
{
  return nanoseconds_to_pretty (nanoseconds.count ());
}

inline
std::string nanoseconds_to_pretty (Nanoseconds::rep nanoseconds)
{
  if (nanoseconds == Nanoseconds::max ().count ())
  {
    return "-";
  }
  else if (nanoseconds == Nanoseconds::min ().count ())
  {
    return "-";
  }
  else if (nanoseconds < 1e3)
  {
    return (boost::str (boost::format ("%4d ns ") % nanoseconds));
  }
  else if (nanoseconds < 1e6)
  {
    double usecs = static_cast<double> (nanoseconds) / 1.0e3;
    return (boost::str (boost::format ("%4.0f us ") % usecs));
  }
  else if (nanoseconds < 1e9)
  {
    double msecs = static_cast<double> (nanoseconds) / 1.0e6;
    return (boost::str (boost::format ("%4.0f ms ") % msecs));
  }
  else if (nanoseconds < (1e9*60))
  {
    double secs = static_cast<double> (nanoseconds) / 1.0e9;
    return (boost::str (boost::format ("%4.0f s ") % secs));
  }

  double mins = static_cast<double> (nanoseconds) / (1.0e9*60.);
  return (boost::str (boost::format ("%4.0f mins ") % mins));
}

namespace {

inline
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

inline
Latency::Latency ()
: m_empty (empty_quantiles ())
, m_quantiles (empty_quantiles ())
{ }

inline
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

  m_file.open (file_path.string (), std::ios::app|std::ios_base::out);

  ASSERT_SS (m_file.is_open (), "Failed to open file: " + file_path.string ());

  BOOST_LOG_TRIVIAL (info) << "Latency file: " << file_path.string ();

  write_header ();
}

inline
Latency::~Latency ()
{
  write_data ();

  stop ();
}

inline
void Latency::enable (bool enable)
{
  if (!enable)
  {
    stop ();
  }
}

inline
void Latency::stop ()
{
  m_stop = true;
}

inline
void Latency::reset ()
{
  m_quantiles = m_empty;
  m_min       = Nanoseconds::max ();
  m_max       = Nanoseconds::min ();
}

inline
void Latency::next (Nanoseconds nanoseconds)
{
  if (m_stop)
  {
    return;
  }

  for (auto &quantile : m_quantiles)
  {
    quantile.second (nanoseconds.count ());
  }

  m_min = std::min (m_min, nanoseconds);
  m_max = std::max (m_max, nanoseconds);
}

inline
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

inline
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

inline
std::string Latency::to_string () const
{
  int64_t median = ba::p_square_quantile (m_quantiles.at (50));
  return nanoseconds_to_pretty (min ()) + ":" +
         nanoseconds_to_pretty (median) + ":" +
         nanoseconds_to_pretty (max ());
}

inline
std::vector<std::string> Latency::to_strings () const
{
  if (m_stop)
  {
    return {};
  }

  std::vector<std::string> stats;

  stats.push_back (
      (boost::format ("%s %s") % "Percentile" % "Latency").str ());
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