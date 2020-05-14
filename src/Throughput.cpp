#include "Throughput.h"
#include "Time.h"
#include "detail/SharedMemory.h"

#include <boost/filesystem.hpp>

#include <iostream>
#include <iomanip>

namespace fs = boost::filesystem;

namespace spmc {

Throughput::Throughput ()
{ }

void Throughput::enable (bool enable)
{
  if (!m_enabled && enable && !m_path.empty ())
  {
    if (!fs::exists (m_path))
    {
      fs::create_directories (fs::path (m_path));

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

bool Throughput::enabled () const
{
  return m_enabled;
}

void Throughput::path (const std::string &directory, const std::string &name)
{
  m_path = directory + "/" + name;
}

void Throughput::reset ()
{
  m_header     = 0;
  m_payload    = 0;
  m_messages   = 0;
  m_dropped    = 0;
  m_bytes      = 0;
  m_start      = INVALID_TIME_POINT;
}

void Throughput::write_header ()
{
  assert (m_file.is_open ());

  m_file
    << "avg_message_size,megabytes_per_sec,messages_per_sec,messages_dropped\n";
}

Throughput &Throughput::write_data ()
{
  if (!m_file.is_open () || !m_enabled)
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
  if (!m_enabled)
  {
    return;
  }

  m_payload += payload;

  next (header + payload, seqNum);
}

void Throughput::next (uint64_t bytes, uint64_t seqNum)
{
  if (!m_enabled)
  {
    return;
  }

  if (m_start == INVALID_TIME_POINT)
  {
    m_start = Clock::now ();
  }
  // reset seqNum on startup and if the server was restarted
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
  if (m_start == INVALID_TIME_POINT)
  {
    return 0;
  }

  return (m_bytes / (1024. * 1024.)) / SecondsFP (time - m_start).count ();
}

uint32_t Throughput::messages_per_sec (TimePoint time) const
{
  if (m_start == INVALID_TIME_POINT)
  {
    return 0;
  }

  return m_messages / SecondsFP (time - m_start).count ();
}

std::string Throughput::to_string () const
{
  if (!m_enabled)
  {
    return "";
  }
  std::stringstream stats;

  auto now = Clock::now ();

  stats << megabytes_per_sec (now)   << " MB/s\t"
        << std::fixed << std::setprecision (3)
        << messages_per_sec (now) / 1.0e6 << "\tM msgs/sec "
        << "dropped=" << m_dropped;

  return stats.str ();
}

} // namespace spmc
