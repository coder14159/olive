#ifndef IPC_THROUGHPUT_H
#define IPC_THROUGHPUT_H

#include "Time.h"

#include <fstream>
#include <string>

namespace spmc {

/*
 * Throughput computes throughput statistics and persist the data to file
 */
class Throughput
{
public:
  Throughput ();

  /*
   * If a path is set, persist throughput statistics to file
   */
  void path (const std::string &directory, const std::string &name);
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
  /*
   * return true if statistics calculation is enabled
   */
  bool enabled () const;

  uint64_t messages () const { return m_messages; }
  uint64_t dropped ()  const { return m_dropped;  }
  uint64_t bytes ()    const { return m_bytes;    }

  uint32_t megabytes_per_sec (TimePoint time) const;
  uint32_t messages_per_sec (TimePoint time) const;

  std::string to_string () const;

  void write_header ();
  Throughput &write_data ();

private:
  bool           m_enabled    = false;

  uint64_t       m_header     = 0;
  uint64_t       m_payload    = 0;
  uint64_t       m_messages   = 0;
  uint64_t       m_dropped    = 0;
  uint64_t       m_bytes      = 0;
  uint64_t       m_seqNum     = 0;

  TimePoint      m_start      = INVALID_TIME_POINT;

  std::ofstream  m_file;
  std::string    m_path;
};

}

#endif // IPC_THROUGHPUT_H
