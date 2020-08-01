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

std::string
throughput_to_pretty (uint64_t bytes, TimeDuration duration)
{
  if (bytes == 0)
  {
    return " - ";
  }

  double bytes_per_second = bytes / to_seconds (duration);

  constexpr double KB = 1024.;
  constexpr double MB = 1024. * 1024.;
  constexpr double GB = 1024. * 1024. *1024.;

  if (bytes_per_second > GB)
  {
    return (boost::format ("%4.1f GB/s") % (bytes_per_second/GB)).str ();
  }
  else if (bytes_per_second > MB)
  {
    return (boost::format ("%4.1f MB/s") % (bytes_per_second/MB)).str ();
  }
  else if (bytes_per_second > KB)
  {
    return (boost::format ("%4.1f KB/s") % (bytes_per_second/KB)).str ();
  }

  return (boost::format ("%4d bytes/s") % bytes_per_second).str ();
}

std::string
message_throughput_to_pretty (uint64_t messages, TimeDuration duration)
{
  if (messages == 0)
  {
    return " - ";
  }

  double messages_per_second = messages / to_seconds (duration);

  constexpr double K = 1.0e3;
  constexpr double M = 1.0e6;
  constexpr double G = 1.0e9;

  if (messages_per_second > G)
  {
    return (boost::format ("%4.1f G msgs/s") % (messages_per_second/G)).str ();
  }
  else if (messages_per_second > M)
  {
    return (boost::format ("%4.1f M msgs/s") % (messages_per_second/M)).str ();
  }
  else if (messages_per_second > K)
  {
    return (boost::format ("%4.1f K msgs/s") % (messages_per_second/K)).str ();
  }

  return (boost::format ("%4d msgs/s") % messages_per_second).str ();
}

Throughput::Throughput ()
{ }

Throughput::Throughput (const std::string &directory,
                        const std::string &filename)
{
  if (!directory.empty () && !fs::exists (directory))
  {
    ASSERT_SS (fs::create_directories (directory),
              "Failed to create directory: " << directory);

    BOOST_LOG_TRIVIAL (info) << "Created directory: " << directory;
  }

  ASSERT(!filename.empty (), "Empty throughput filename");

  fs::path file_path = fs::path (directory) / fs::path (filename);

  m_file.open (file_path.string (), std::ios::app|std::ios_base::out);

  ASSERT_SS (m_file.is_open (), "Failed to open file: " << file_path.string ());

  BOOST_LOG_TRIVIAL (info) << "Throughput file: " << file_path.string ();

  write_header ();
}

Throughput::~Throughput ()
{
  stop ();
}

void Throughput::enable (bool enable)
{
  if (!enable)
  {
    stop ();
  }
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
  if (!m_file.is_open ())
  {
    return;
  }

  m_file
    << "avg_message_size,megabytes_per_sec,messages_per_sec,messages_dropped\n";
}

Throughput &Throughput::write_data ()
{
  if (!m_file.is_open () || !m_bytes || !m_messages)
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
  auto elapsed = m_timer.elapsed ();

  auto throughput = throughput_to_pretty (m_bytes, elapsed)
                  + " " + message_throughput_to_pretty (m_messages, elapsed);

  return throughput;
}

} // namespace spmc
