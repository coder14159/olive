#include "Assert.h"
#include "Throughput.h"
#include "detail/SharedMemory.h"
#include "detail/Utils.h"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/math/special_functions/round.hpp>

#include <iostream>
#include <iomanip>

namespace fs = boost::filesystem;
using boost::math::lround;

namespace olive {
namespace {
  constexpr double KB = 1024.;
  constexpr double MB = 1024. * 1024.;
  constexpr double GB = 1024. * 1024. *1024.;
}

std::string
throughput_bytes_to_pretty (uint64_t bytes, TimeDuration duration)
{
  if (bytes == 0)
  {
    return " - ";
  }

  double bytes_per_second = bytes / to_seconds (duration);


  if (bytes_per_second > GB)
  {
    return (boost::format ("%4.1f GB/s") % std::lround (bytes_per_second/GB)).str ();
  }
  else if (bytes_per_second > MB)
  {
    return (boost::format ("%4.1f MB/s") % std::lround (bytes_per_second/MB)).str ();
  }
  else if (bytes_per_second > KB)
  {
    return (boost::format ("%4.0f KB/s") % std::lround (bytes_per_second/KB)).str ();
  }

  return (boost::format ("%4.0f bytes/s") % std::lround (bytes_per_second)).str ();
}

std::string
throughput_messages_to_pretty (uint64_t messages, TimeDuration duration)
{
  if (messages == 0)
  {
    return " - ";
  }

  double messages_per_second =
    std::lround (static_cast<double> (messages) / to_seconds (duration));

  constexpr double K = 1.0e3;
  constexpr double M = 1.0e6;
  constexpr double G = 1.0e9;

  if (messages_per_second > G)
  {
    return (boost::format ("%4.1f G msgs/s") % std::lround (messages_per_second/G)).str ();
  }
  else if (messages_per_second > M)
  {
    return (boost::format ("%4.1f M msgs/s") % std::lround (messages_per_second/M)).str ();
  }
  else if (messages_per_second > K)
  {
    return (boost::format ("%4.0f K msgs/s") % std::lround (messages_per_second/K)).str ();
  }

  return (boost::format ("%4.0f msgs/s") % messages_per_second).str ();
}

Throughput::Throughput ()
{ }

Throughput::Throughput (const std::string &directory,
                        const std::string &filename)
{
  if (directory.empty () || filename.empty ())
  {
    return;
  }

  CHECK (!directory.empty (), "Empty throughput directory name");
  CHECK (!filename.empty (), " Empty throughput filename");

  if (!fs::exists (directory))
  {
    CHECK_SS (fs::create_directories (directory),
              "Failed to create directory: " << directory);

    BOOST_LOG_TRIVIAL (info) << "Created directory: " << directory;
  }

  fs::path file_path = fs::path (directory) / fs::path (filename);

  bool output_header = !fs::exists (file_path.string ());

  m_file.open (file_path.string (), std::ios::app|std::ios_base::out);

  CHECK_SS (m_file.is_open (), "Failed to open file: " << file_path.string ());

  BOOST_LOG_TRIVIAL (info) << "Throughput file: " << file_path.string ();

  if (output_header)
  {
    write_header ();
  }
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
  m_timer.stop ();
}

bool Throughput::is_stopped () const
{
  return m_stop;
}

bool Throughput::is_running () const
{
  return !m_stop;
}

void Throughput::write_header ()
{
  if (m_stop || !m_file.is_open ())
  {
    return;
  }

  m_file << "avg_message_size,bytes_per_sec,messages_per_sec\n";
}

Throughput &Throughput::write_data ()
{
  if (m_stop || !m_file.is_open () || !m_bytes || !m_messages)
  {
    return *this;
  }

  size_t avgMessageSize = (m_bytes / m_messages);

  m_file << avgMessageSize      << ","
         << bytes_per_sec ()    << ','
         << messages_per_sec () << '\n';

  return *this;
}

uint32_t Throughput::bytes_per_sec () const
{
  if (m_bytes == 0)
  {
    return 0;
  }

  return lround (m_bytes / to_seconds (m_timer.elapsed ()));
}

uint32_t Throughput::megabytes_per_sec () const
{
  if (m_bytes == 0)
  {
    return 0;
  }

  return lround (bytes_per_sec ()/MB);
}

uint32_t Throughput::messages_per_sec () const
{
  if (m_messages == 0)
  {
    return 0;
  }

  double seconds = to_seconds (m_timer.elapsed ());

  return lround (static_cast<double> (m_messages)/seconds);
}

std::string Throughput::to_string () const
{
  auto duration = m_timer.elapsed ();

  std::string stats = throughput_bytes_to_pretty (m_bytes, duration) + " " +
                      throughput_messages_to_pretty (m_messages, duration);
  return stats;
}

} // namespace olive
