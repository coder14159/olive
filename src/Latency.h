#ifndef IPC_LATENCY_H
#define IPC_LATENCY_H

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace spmc {

/*
 * Compute latency quantiles and persist statistics to file.
 *
 * All latency values are in nanoseconds.
 */
class Latency
{
public:

  using Quantile =
    boost::accumulators::accumulator_set<float,
                boost::accumulators::stats<
                  boost::accumulators::tag::p_square_quantile>>;
public:
  Latency ();

  /*
   * Latency measurement is disabled by default
   */
  void enable (bool enable);
  /*
   * returns true if statistics calculation is enabled
   */
  bool enabled () const;
  /*
   * Set a directory and file name in which to output latency files
   */
  void path (const std::string &directory, const std::string &name);

  /*
   * Return a vector of human human readable strings for representing each
   * latency quantile
   */
  std::vector<std::string> to_strings () const;

  /*
   * Reset the internal latency stats. Not thread safe.
   */
  void reset ();

  /*
   * Add a latency value for statistics calculation
   */
  void latency (int64_t nanoseconds);

  const std::map<float, Quantile>& quantiles () const { return m_quantiles; }

  int64_t min () const { return m_min; }
  int64_t max () const { return m_max; }

  void write_data ();

private:

  void init_file ();

  void write_header ();

private:
  std::map<float, Quantile> m_quantiles;
  std::map<float, Quantile> m_empty; // empty quantiles used for resetting
  std::string               m_path;
  std::ofstream             m_file;
  bool                      m_enabled = false;
  int64_t                   m_min = std::numeric_limits<int64_t>::max ();
  int64_t                   m_max = std::numeric_limits<int64_t>::min ();
};

} // namespace spmc

#endif
