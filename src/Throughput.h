#ifndef IPC_THROUGHPUT_H
#define IPC_THROUGHPUT_H

#include "Chrono.h"
#include "Timer.h"

#include <fstream>
#include <string>
#include <vector>

namespace spmc {

/*
 * Create a pretty format string for throughput
 */
std::string throughput_to_pretty (uint64_t bytes, TimeDuration duration);

/*
 * Computes message throughput and persist data to file
 *
 * Default behaviour is to do nothing unless the enable () method is called
 *
 * Not thread synchronised.
 */
class Throughput
{
public:

  /*
   * Compute throughput values which can be requested via member function calls
   */
  Throughput ();

  /*
   * Computes throughput information and writes data to disk in csv format
   */
  Throughput (const std::string &directory, const std::string &filename);

  void stop ();

  bool is_stopped () const;
  /*
   * Persist throughput statistics to file if a path is set
   */
  void write (const std::string &directory, const std::string &filename);
  /*
   * Update byte count for throughput calculation
   *
   * Sequence number is used to calculate dropped messages
   */
  void next (uint64_t bytes, uint64_t seqNum);
  /*
   * Update byte counts for throughput calculation
   *
   * Sequence number is used to calculate dropped messages
   */
  void next (uint64_t header, uint64_t payload, uint64_t seqNum);
  /*
   * Reset statistics
   */
  void reset ();

  /*
   * Throughput computation is disabled by default
   */
  void enable (bool enable);

  uint64_t messages () const { return m_messages; }
  uint64_t dropped ()  const { return m_dropped;  }
  uint64_t bytes ()    const { return m_bytes;    }

  uint32_t megabytes_per_sec (TimePoint time) const;
  uint32_t messages_per_sec (TimePoint time) const;

  /*
   * Return a throughput string since start or last reset
   */
  std::string to_string () const;

  void write_header ();

  Throughput &write_data ();

public:

private:

  uint64_t       m_header     = 0;
  uint64_t       m_payload    = 0;
  uint64_t       m_messages   = 0;
  uint64_t       m_dropped    = 0;
  uint64_t       m_bytes      = 0;
  uint64_t       m_seqNum     = 0;

  Timer m_timer;

  std::ofstream  m_file;

  bool m_stop = false;
};

}

#endif // IPC_THROUGHPUT_H
