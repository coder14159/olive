#include "Assert.h"
#include "Throughput.h"
#include "detail/SharedMemory.h"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <iostream>
#include <iomanip>

namespace fs = boost::filesystem;

namespace spmc {

Throughput::Throughput ()
{ }

Throughput::Throughput (const std::string &directory,
                        const std::string &filename)
{
  if (!directory.empty () && !fs::exists (directory))
  {
    CHECK_SS (fs::create_directories (directory),
              "Failed to create directory: " << directory);
  }

  ASSERT(!filename.empty (), "Empty throughput filename");

  fs::path path = fs::path (directory) / fs::path (filename);

  m_file.open (path.string (), std::ios::app|std::ios_base::out);

  CHECK_SS(!m_file.fail (), "Failed to open file: " << filename);

  write_header ();
}

void Throughput::stop ()
{
  m_stop = true;
}

bool Throughput::is_stopped () const
{
  return m_stop;
}

void Throughput::reset ()
{
  m_header     = 0;
  m_payload    = 0;
  m_messages   = 0;
  m_dropped    = 0;
  m_bytes      = 0;

  m_timer.reset ().start ();
}

void Throughput::write_header ()
{
  assert (m_file.is_open ());

  m_file
    << "avg_message_size,megabytes_per_sec,messages_per_sec,messages_dropped\n";
}

Throughput &Throughput::write_data ()
{
  if (!m_file || !m_file.is_open () || !m_bytes || !m_messages)
  {
    return *this;
  }

  auto now = Clock::now ();

  size_t avgPayload = (m_payload > 0) ? (m_payload / m_messages)
                                      : (m_bytes   / m_messages);

  m_file << avgPayload  << ","
         << megabytes_per_sec (now) << ','
         << messages_per_sec (now)  << ','
         << dropped ()           << '\n';

  return *this;
}

void Throughput::next (uint64_t header, uint64_t payload, uint64_t seqNum)
{
  m_payload += payload;

  next (header + payload, seqNum);
}

void Throughput::next (uint64_t bytes, uint64_t seqNum)
{
  if (m_stop)
  {
    return;
  }

  /*
   * Reset on startup or if the server was restarted
   */
  if (m_seqNum == 0 || seqNum == 1)
  {
    reset ();

    m_seqNum = seqNum;
  }
  else
  {
    uint64_t diff = (seqNum - m_seqNum);

    if (diff > 1)
    {
      m_dropped += diff;
    }

    m_seqNum = seqNum;

    ++m_messages;

    m_bytes += bytes;
  }
}

uint32_t Throughput::megabytes_per_sec (TimePoint time) const
{
  if (m_bytes == 0)
  {
    return 0;
  }

  auto seconds = to_seconds (m_timer.elapsed ());

  return (m_bytes / (1024. * 1024.)) / seconds;
}

uint32_t Throughput::messages_per_sec (TimePoint time) const
{
  if (m_bytes == 0)
  {
    return 0;
  }

  double seconds = to_seconds (m_timer.elapsed ());

  auto thruput = (static_cast<double> (m_messages)/seconds);

  return thruput;
}

std::string Throughput::to_string () const
{
  std::stringstream stats;

  auto now = Clock::now ();

  stats << megabytes_per_sec (now) << " MB/s "
      << std::fixed << std::setprecision (3)
      << messages_per_sec (now) / 1.0e6 << " M msgs/sec "
      << m_dropped << " msgs dropped";

  return stats.str ();
}

std::vector<std::string> Throughput::to_strings () const
{
  auto now = Clock::now ();

  std::vector<std::string> stats;

  stats.push_back (
    (boost::format ("%-13s %.3f MB/sec %.3f M msgs/sec")
            % "throughput:"
            % megabytes_per_sec (now)
            % (messages_per_sec (now) / 1.0e6)).str ());

  stats.push_back (
    (boost::format ("%-13s %d") % "msgs dropped:" % m_dropped).str ());

  return stats;
}

} // namespace spmc
