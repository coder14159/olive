#ifndef IPC_LATENCY_H
#define IPC_LATENCY_H

#include "Chrono.h"

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace spmc {

std::string nanoseconds_to_pretty (int64_t nanoseconds);

std::string nanoseconds_to_pretty (Nanoseconds nanoseconds);

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
  /*
   * Compute latency data and persists it to file in the named directory
   */
  Latency (const std::string &directory, const std::string &filename);

  ~Latency ();

  void enable (bool enable);

  /*
   * Stop latency computation
   */
  void stop ();

  /*
   * Return true if latency calculation is stopped
   */
  bool is_stopped () const { return m_stop; }

  /*
   * Return true if latency calculation is not stopped
   */
  bool is_running () const { return (m_stop == false); }

  /*
   * Return a string representing minimum:median:max latencies since the start
   * or last reset
   */
  std::string to_string () const;

  /*
   * Return a vector of human human readable strings for representing each
   * latency quantile
   */
  std::vector<std::string> to_strings () const;

  /*
   * Add a latency value for statistics calculation
   */
  void next (Nanoseconds nanoseconds);

  /*
   * Return map of latency values for a pre-determined set of quantiles
   */
  const std::map<float, Quantile>& quantiles () const { return m_quantiles; }

  /*
   * Minimum latency value
   */
  Nanoseconds min () const { return m_min; }
  /*
   * Maximum latency value
   */
  Nanoseconds max () const { return m_max; }

  /*
   * Write quantile values to disk
   */
  Latency &write_data ();

  /*
   * Reset the the latency the values
   * Called on receipt of a RESET_INTERVAL token on the latency queue
   *
   * This should only be called from the same context as the thread calling
   * the Latency::write_xxx () method
   */
  void reset ();

private:

  Latency (const Latency &) = delete;

  Latency & operator=(const Latency&) = delete;

  void init_quantiles ();

  void init_file ();

  void write_header ();

private:
  const std::map<float, Quantile> m_empty; // empty quantiles used for resetting
  std::map<float, Quantile> m_quantiles;
  std::ofstream             m_file;
  bool                      m_stop = false;
  Nanoseconds               m_min  = Nanoseconds::max ();
  Nanoseconds               m_max  = Nanoseconds::min ();
};

} // namespace spmc

#include "Latency.inl"

#endif
