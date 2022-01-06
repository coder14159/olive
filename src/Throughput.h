#ifndef IPC_THROUGHPUT_H
#define IPC_THROUGHPUT_H

#include "Chrono.h"
#include "Timer.h"

#include <fstream>
#include <string>
#include <vector>

namespace spmc {

/*
 * Create a pretty format string for bytes throughput
 */
std::string
throughput_bytes_to_pretty (uint64_t bytes, TimeDuration duration);

/*
 * Create a pretty format string for message throughput
 */
std::string
throughput_messages_to_pretty (uint64_t messages, TimeDuration duration);

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
  /*
   * Move constructor
   */
  Throughput(const Throughput &&throughput);

  ~Throughput ();

  void stop ();

  bool is_stopped () const;
  bool is_running () const;
  /*
   * Persist throughput statistics to file if a path is set
   */
  void write (const std::string &directory, const std::string &filename);
  /*
   * Update byte and message count
   */
  void next (uint64_t bytes, uint64_t messages);
  /*
   * Reset statistics
   */
  void reset ();
  /*
   * Throughput computation is disabled by default
   */
  void enable (bool enable);
  /*
   * Total number of messages consumed
   */
  uint64_t messages () const { return m_messages; }
  /*
   * Total count of bytes consumed
   */
  uint64_t bytes () const { return m_bytes; }
  /*
   * Return throughput in MB/sec
   */
  uint32_t megabytes_per_sec () const;
  /*
   * Return throughput in bytes/sec
   */
  uint32_t bytes_per_sec () const;
  /*
   * Return throughput in message/sec
   */
  uint32_t messages_per_sec () const;
  /*
   * Return a human readable throughput string since start or last reset
   */
  std::string to_string () const;
  /*
   * Write header line to file
   */
  void write_header ();
  /*
   * Write header line to file
   */
  Throughput &write_data ();

private:

  uint64_t m_messages = 0;
  uint64_t m_bytes    = 0;

  bool m_stop = { false };

  Timer m_timer;

  std::ofstream m_file;
};

}

#include "Throughput.inl"

#endif // IPC_THROUGHPUT_H
