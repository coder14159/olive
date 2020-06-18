#include "LatencyStats.h"
#include "TimeDuration.h"

#include <boost/cstdint.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include <algorithm>

namespace ba = boost::accumulators;

namespace spmc {

Latency::Latency ()
{
  m_quantiles[1]     = Quantile (ba::quantile_probability = 0.01);
  m_quantiles[5]     = Quantile (ba::quantile_probability = 0.05);
  m_quantiles[25]    = Quantile (ba::quantile_probability = 0.25);
  m_quantiles[50]    = Quantile (ba::quantile_probability = 0.50);
  m_quantiles[75]    = Quantile (ba::quantile_probability = 0.70);
  m_quantiles[80]    = Quantile (ba::quantile_probability = 0.80);
  m_quantiles[90]    = Quantile (ba::quantile_probability = 0.90);
  m_quantiles[95]    = Quantile (ba::quantile_probability = 0.95);
  m_quantiles[99]    = Quantile (ba::quantile_probability = 0.99);
  m_quantiles[99.5]  = Quantile (ba::quantile_probability = 0.995);
  m_quantiles[99.6]  = Quantile (ba::quantile_probability = 0.996);
  m_quantiles[99.7]  = Quantile (ba::quantile_probability = 0.997);
  m_quantiles[99.8]  = Quantile (ba::quantile_probability = 0.998);
  m_quantiles[99.9]  = Quantile (ba::quantile_probability = 0.999);
  m_quantiles[99.95] = Quantile (ba::quantile_probability = 0.9995);
  m_quantiles[99.99] = Quantile (ba::quantile_probability = 0.9999);

  m_empty = m_quantiles;
}

void Latency::enable (bool enable)
{
  if (!m_enabled && enable && !m_path.empty ())
  {
    if (!boost::filesystem::exists (m_path))
    {
      m_file.open (m_path);

      write_header ();
    }
    else
    {
      m_file.open (m_path.c_str (), std::ios::app|std::ios_base::out);
    }

    assert (m_file);
  }

  m_enabled = enable;
}

bool Latency::enabled () const
{
  return m_enabled;
}

void Latency::path (const std::string &directory, const std::string &name)
{
  m_path = directory + "/" + name;
}

void Latency::reset ()
{
  if (!m_enabled)
  {
    return;
  }

  m_quantiles = m_empty;
  m_min       = std::numeric_limits<int64_t>::max ();
  m_max       = std::numeric_limits<int64_t>::min ();
}

void Latency::latency (int64_t nanoseconds)
{
  if (!m_enabled)
  {
    return;
  }

  for (auto &quantile : m_quantiles)
  {
    quantile.second (nanoseconds);
  }

  m_min = std::min (m_min, nanoseconds);
  m_max = std::max (m_max, nanoseconds);
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

void Latency::write_data ()
{
  if (!m_file.is_open () || !m_enabled)
  {
    return;
  }

  m_file << m_min;

  for (const auto &quantile : m_quantiles)
  {
    m_file << ","
           << static_cast<int64_t>(ba::p_square_quantile (quantile.second));
  }

  m_file << "," << m_max << "\n";
}

std::vector<std::string> Latency::to_strings () const
{
  if (!m_enabled)
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
