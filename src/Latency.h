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

  /*
   * Computes latency information
   */
  Latency ();

  ~Latency ();

  /*
   * Compute latency data and persists it to file in the named directory
   */
  Latency (const std::string &directory, const std::string &filename);

  /*
   * Stop latency computation
   */
  void stop ();

  /*
   * Return true if latency calculation has been stopped
   */
  bool is_stopped () const { return m_stop; }

  /*
   * Return a vector of human human readable strings for representing each
   * latency quantile
   */
  std::vector<std::string> to_strings () const;

  /*
   * Add a latency value for statistics calculation
   */
  void latency (int64_t nanoseconds);

  /*
   * Return map of latency values for a pre-determined set of quantiles
   */
  const std::map<float, Quantile>& quantiles () const { return m_quantiles; }

  /*
   * Minimum latency value
   */
  int64_t min () const { return m_min; }
  /*
   * Masximum latency value
   */
  int64_t max () const { return m_max; }

  /*
   * Write quantile values to disk
   */
  Latency &write_data ();

  /*
   * Reset the the latency the values
   * Called on receipt of a RESET_INTERVAL token on the latency queue
   *
   * This should only be called from the same context as the thread calling
   * the Latency::write () method
   */
  void reset ();

private:

  void init_quantiles ();

  void init_file ();

  void write_header ();

private:
  const std::map<float, Quantile> m_empty; // empty quantiles used for resetting
  std::map<float, Quantile> m_quantiles;
  std::string               m_directory;
  std::string               m_path;
  std::ofstream             m_file;
  bool                      m_stop = false;
  int64_t                   m_min  = std::numeric_limits<int64_t>::max ();
  int64_t                   m_max  = std::numeric_limits<int64_t>::min ();
};

} // namespace spmc

#endif
