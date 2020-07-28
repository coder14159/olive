#include "Buffer.h"
#include "Chrono.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SPMCSink.h"
#include "SPMCStream.h"
#include "Throttle.h"
#include "TimeDuration.h"
#include "Timer.h"
#include "detail/SharedMemory.h"

#include <boost/circular_buffer.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/log/trivial.hpp>

#include <cstdlib>
#include <ext/numeric>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE SPMCTestPerformance

#define SPMC_DEBUG_ASSERT

#include <boost/test/unit_test.hpp>

using namespace std::chrono_literals;
using namespace boost::log::trivial;

using namespace spmc;

/*
 * Override time to run each performance test from the environment variable
 * "TIMEOUT" which should contain a value in seconds.
 */
TimeDuration get_test_duration ()
{
  Nanoseconds duration (100ms);

  if (getenv ("TIMEOUT") != nullptr)
  {
    double seconds = std::atof (getenv ("TIMEOUT"));

    int64_t ns = seconds * 1e9;

    duration = Nanoseconds (ns);
  }

  return std::chrono::duration_cast<Nanoseconds> (duration);
}

BOOST_AUTO_TEST_CASE (TestTimeDuration)
{
  const auto start = Clock::now ();

  const int count = 50000;
  TimePoint now;

  for (int i = 0; i < count; ++i)
  {
    now = Clock::now ();
  }

  auto elapsed = nanoseconds_to_pretty (now - start);

  auto per_call = (Nanoseconds (now - start) / count);

  BOOST_TEST_MESSAGE ("Duration Clock::now () per call: "
      << nanoseconds_to_pretty (per_call) << " nanoseconds");

  /*
   * Max time for a wall clock call is around 70 ns on a 3.10GHz CPU
   *
   * Note it is possible to implement a faster clock using the rdtsc instruction
   */
  BOOST_CHECK (per_call.count () < 200);
}

BOOST_AUTO_TEST_CASE (ThroughputCircularBuffers)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = get_test_duration ();

  auto stress_boost_circular_buffer = [&duration] (
    size_t bufferSize, size_t messageSize) -> double
  {
    BOOST_CHECK (bufferSize > messageSize);

    std::vector<uint8_t> message (messageSize);

    boost::circular_buffer<uint8_t> buffer (bufferSize);

    std::vector<uint8_t> out;
    out.reserve (bufferSize);

    uint64_t bytes = 0;

    Timer timer;

    for (int i = 0; ; ++i)
    {
      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        timer.elapsed ().pretty ();
        break;
      }
      out.clear ();

      // insert data to the buffer
      buffer.insert (buffer.end (), message.begin (), message.end ());

      // copy data out of circular buffer
      std::copy (buffer.begin (), buffer.end (), std::back_inserter (out));

      // erase copied data from the buffer
      buffer.erase (buffer.begin (), buffer.begin () + buffer.size ());

      bytes += out.size ();

      ++i;
    }

    timer.stop ();

    auto throughput = static_cast<double> (bytes) / (1024.*1024.*1024.)
                    / to_seconds (timer.elapsed ());

    BOOST_TEST_MESSAGE ("    boost  buffer: "
                        << std::fixed << std::setprecision (3)
                        << throughput << " GB/s");

    return throughput;
  };

  auto stress_custom_buffer = [&duration] (
    size_t bufferSize, size_t messageSize) -> double
  {
    BOOST_CHECK (bufferSize > messageSize);

    std::vector<uint8_t> message (messageSize);

    Buffer<std::allocator<uint8_t>> buffer (bufferSize);
    std::vector<uint8_t> out (bufferSize);

    uint64_t bytes = 0;

    Timer timer;

    for (int i = 0; ; ++i)
    {
      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        timer.elapsed ().pretty ();
        break;
      }

      // insert data to the buffer
      buffer.push (message.data (), message.size ());

      // copy data out of circular buffer
      buffer.pop (out, message.size ());

      bytes += out.size ();

      ++i;
    }

    timer.stop ();

    auto throughput = static_cast<double> (bytes) / (1024.*1024.*1024.)
                    / to_seconds (timer.elapsed ());

    BOOST_TEST_MESSAGE ("    custom buffer: "
                        << std::fixed << std::setprecision (3)
                        << throughput << " GB/s");
    return throughput;
  };

  for (size_t bufferSize : { 10000, 20480, 409600})
  {
    BOOST_TEST_MESSAGE ("buffer size: " << bufferSize);

    for (size_t messageSize : { 128, 512, 1024, 2048, 4096 })
    {
      BOOST_TEST_MESSAGE ("  message size: " << messageSize);

      stress_boost_circular_buffer (bufferSize, messageSize);

      stress_custom_buffer (bufferSize, messageSize);
    }
  }
}

template<class Vector>
double VectorThroughput (size_t bufferSize, size_t batchSize)
{
  Buffer<std::allocator<uint8_t>> buffer (bufferSize);
  std::vector<uint8_t> in (batchSize);
  Vector out;

  uint64_t i = 0;
  uint64_t bytes = 0;

  auto duration = get_test_duration ();

  Timer timer;

  while (true)
  {
    if ((i % 1000) == 0 && duration < timer.elapsed ())
    {
      break;
    }

    buffer.push (in.data (), in.size ());

    out.resize (in.size ());
    buffer.pop (out.data (), in.size ());
    bytes += out.size ();

    ++i;
  }
  timer.stop ();

  return static_cast<double> (bytes) / (1024.*1024.*1024.)
                / to_seconds (timer.elapsed ());
};

BOOST_AUTO_TEST_CASE (VectorThroughput)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto run_test = [] (size_t batchSize) {
    using namespace boost::container;

    size_t bufferSize = 204800;

    double throughput =
      VectorThroughput<std::vector<uint8_t>> (bufferSize, batchSize);
    BOOST_TEST_MESSAGE ("Throughput std::vector\t\tsize=" << batchSize << " "
      << std::fixed << std::setprecision (3) << throughput << " GB/s");

    throughput =
      VectorThroughput<small_vector<uint8_t, 1024>> (bufferSize, batchSize);
    BOOST_TEST_MESSAGE ("Throughput small_vector\t\tsize=" << batchSize << " "
      << std::fixed << std::setprecision (3) << throughput << " GB/s");

    if (batchSize <= 1024)
    {
      throughput =
        VectorThroughput<static_vector<uint8_t, 1024>> (bufferSize, batchSize);
      BOOST_TEST_MESSAGE ("Throughput static_vector\tsize=" << batchSize << " "
        << std::fixed << std::setprecision (3) << throughput << " GB/s");
    }
    BOOST_TEST_MESSAGE ("---");
  };

  run_test (128);
  run_test (512);
  run_test (1024);
  run_test (2048);
  run_test (4096);

  BOOST_CHECK (true);
}

void sink_stream_in_single_process (
    size_t            capacity,
    PerformanceStats &stats,
    uint32_t          rate,
    size_t            prefetch)
{
  std::atomic<bool> stop = { false };

  SPMCSinkThread   sink (capacity);
  SPMCStreamThread stream (sink.queue (), false, prefetch);

  auto producer = std::thread ([&stop, &sink, &rate] () {
    Throttle throttle (rate);

    size_t bbaSize = 128;
    std::vector<uint8_t> message (bbaSize, 0);

    std::iota (std::begin (message), std::end (message), 1);

    while (!stop)
    {
      sink.next (message);

      throttle.throttle ();
    }
  });

  std::this_thread::sleep_for (5ms);

  auto consumer = std::thread ([&stop, &stream, &stats] () {
    Header header;

    std::vector<uint8_t> data;

    while (!stop)
    {
      if (stream.next (header, data))
      {
        stats.update (sizeof (Header), data.size (), header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));
      }
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stop = true;

  stream.stop ();
  consumer.join ();

  sink.stop ();
  producer.join ();
}

template <class POD>
void sink_stream_in_single_process_pod (
    size_t            capacity,
    PerformanceStats &stats,
    uint32_t          rate,
    size_t            prefetch)
{
  std::atomic<bool> stop = { false };

  SPMCSinkThread   sink (capacity);
  SPMCStreamThread stream (sink.queue (), false, prefetch);

  auto producer = std::thread ([&stop, &sink, &rate] () {

    Throttle throttle (rate);

    POD payload;

    while (!stop)
    {
      sink.next (payload);

      throttle.throttle ();
    }
  });

  std::this_thread::sleep_for (5ms);

  auto consumer = std::thread ([&stop, &stream, &stats] () {

    Header header;

    std::vector<uint8_t> data;

    while (!stop)
    {
      if (stream.next (header, data))
      {
        stats.update (sizeof (Header), data.size (), header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));
      }
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stop = true;

  stream.stop ();
  consumer.join ();

  sink.stop ();
  producer.join ();
}

BOOST_AUTO_TEST_CASE (ThroughputSinkStreamMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 2048000;
  size_t rate     = 0;
  size_t prefetch = 0;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << (rate/1.0e6) << " M messages/sec");
}

BOOST_AUTO_TEST_CASE (LatencySinkStreamMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 20480;
  size_t rate     = 1e6;
  size_t prefetch = 0;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << (rate/1.0e6) << " M messages/sec");

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThroughputSinkStreamWithPrefetchMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }
  /*
   * Using a prefetch size of around 1KB provides good throughput improvements
   * when transmitting smaller messages at the cost of a higher mean latency.
   */

  spmc::ScopedLogLevel log (error);

  size_t capacity = 2048000;
  size_t rate     = 0;
  size_t prefetch = 1024;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 2e6);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (LatencySinkStreamWithPrefetchMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }
  /*
   * Using a prefetch size of around 1KB provides good throughput improvements
   * when transmitting smaller messages at the cost of a higher mean latency.
   */
  spmc::ScopedLogLevel log (error);

  size_t capacity = 20480;
  size_t rate     = 1e6;
  size_t prefetch = 1024;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate " << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 500000);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThroughputSinkStreamPODMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 2048000;
  size_t rate     = 0;
  size_t prefetch = 1024;

  PerformanceStats stats;

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 1e6);
}

BOOST_AUTO_TEST_CASE (LatencySinkStreamPODMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 20480;
  size_t rate     = 1e6;
  size_t prefetch = 0;

  PerformanceStats stats;

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 500000);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThroughputSinkStreamPODWithPrefetchMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 2048000;
  size_t rate     = 0;
  size_t prefetch = 1024;

  PerformanceStats stats;

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 1e6);
}

BOOST_AUTO_TEST_CASE (LatencySinkStreamPODWithPrefetchMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 20480;
  size_t rate     = 1e6;
  size_t prefetch = 1024;

  PerformanceStats stats;

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1e6) << " M messages/sec");

  BOOST_CHECK (rate > 500000);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

void sink_stream_in_shared_memory (
    size_t            capacity,
    PerformanceStats &stats,
    uint32_t          rate,
    size_t            prefetch)
{
  using namespace boost;
  using namespace boost::interprocess;

  std::string name = "SinkStreamSharedMemory:Perf";

  struct RemoveSharedMemory
  {
    RemoveSharedMemory (const std::string & name) : name (name)
    { shared_memory_object::remove (name.c_str ()); }

    ~RemoveSharedMemory ()
    { shared_memory_object::remove (name.c_str ()); }

    std::string name;
  } cleanup (name);

  Throttle throttle (rate);

  /*
   * Message size in bytes
   */
  size_t messageSize = 128;

  /*
   * Create shared memory in which shared objects can be created
   */
  managed_shared_memory (open_or_create, name.c_str(),
                         capacity + SharedMemory::BOOK_KEEPING);

  SPMCSinkProcess sink (name, name + ":queue", capacity);

  std::atomic<bool> stop = { false };

  auto producer = std::thread ([&stop, &sink, &messageSize, &throttle] () {

    std::vector<uint8_t> message (messageSize, 0);

    std::iota (std::begin (message), std::end (message), 1);

    while (!stop)
    {
      sink.next (message);
      throttle.throttle ();
    }
  });

  bool allowMessageDrops = false;

  SPMCStreamProcess stream (name, name + ":queue",
                            allowMessageDrops, prefetch);

  auto consumer = std::thread ([&stream, &stats, &messageSize] () {
    Header header;

    std::vector<uint8_t> message (messageSize, 0);

    while (true)
    {
      if (stream.next (header, message))
      {
        stats.update (header.size+sizeof (Header), header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));

        message.clear ();
      }
      else
      {
        break;
      }
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stream.stop ();
  sink.stop ();

  stop = true;

  // consume messages from the shared memory queue
  producer.join ();
  consumer.join ();

}

BOOST_AUTO_TEST_CASE (ThroughputSinkStreamMultiProcess)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  PerformanceStats stats;

  size_t capacity = 2048000;
  uint32_t rate   = 0;
  size_t prefetch = 0;

  sink_stream_in_shared_memory (capacity, stats, rate, prefetch);

  auto megabytes_per_sec = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (megabytes_per_sec > 500);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (megabytes_per_sec/1024.) << " GB/sec");

  auto messages_per_sec = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (messages_per_sec/1.0e6) << " M messages/sec");
}

BOOST_AUTO_TEST_CASE (ThroughputSinkStreamWithPrefetchMultiProcess)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  PerformanceStats stats;

  size_t capacity = 2048000;
  uint32_t rate   = 0;
  size_t prefetch = 10240;

  sink_stream_in_shared_memory (capacity, stats, rate, prefetch);

  auto megabytes_per_sec = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (megabytes_per_sec > 1000);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (megabytes_per_sec/1024.) << " GB/sec");

  auto messages_per_sec = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (messages_per_sec/1.0e6) << " M messages/sec");
}

BOOST_AUTO_TEST_CASE (LatencySinkStreamMultiProcess)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  PerformanceStats stats;

  size_t capacity = 20480;
  uint32_t rate   = 1e6;
  size_t prefetch = 0;

  sink_stream_in_shared_memory (capacity, stats, rate, prefetch);

  auto megabytes_per_sec = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Clock::now ());

  BOOST_CHECK (megabytes_per_sec > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (megabytes_per_sec/1024.) << " GB/sec");

  auto messages_per_sec = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Clock::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (messages_per_sec/1.0e6) << " M messages/sec");

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThreadIdentity)
{
  int thread_id_count = 0;
  int thread_specific_count = 0;
  int thread_local_count = 0;
  int bare_count = 0;
  {
    std::atomic<bool> stop = { false };
    std::thread::id dummy;

    auto test_thread_id = std::thread ([&stop, &thread_id_count, &dummy] () {
      while (!stop)
      {
        for (int i = 0; i < 1e6; ++i)
        {
          auto id = std::this_thread::get_id ();
          dummy = id;
        }
        ++thread_id_count;
      }
    });

    std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

    stop = true;
    test_thread_id.join ();

    BOOST_TEST_MESSAGE ("thread::get_id() count=" << thread_id_count);
  }
  {
    std::atomic<bool> stop = { false };
    int dummy = 0;
    stop  = false;

    auto test_thread_specific = std::thread ([&stop, &thread_specific_count,
                                          &dummy] () {
      boost::thread_specific_ptr<int> thread_id;

      thread_id.reset (new int (0));

      while (!stop)
      {
        for (int i = 0; i < 1e6; ++i)
        {
          auto id = *thread_id;
          dummy = id;
        }
        ++thread_specific_count;
      }
    });

    std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

    stop = true;
    test_thread_specific.join ();

    BOOST_TEST_MESSAGE ("boost::thread_specific_ptr count="
                        << thread_specific_count);
  }
  {
    std::atomic<bool> stop = { false };
    int dummy = 0;
    stop  = false;

    auto test_thread_local = std::thread ([&stop, &thread_local_count,
                                          &dummy] () {


      thread_local int local = 0;

      while (!stop)
      {
        for (int i = 0; i < 1e6; ++i)
        {
          auto id = local;
          dummy = id;
        }
        ++thread_local_count;
      }
    });

    std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

    stop = true;
    test_thread_local.join ();
    BOOST_TEST_MESSAGE ("thread_local count=" << thread_local_count);
  }
  {
    std::atomic<bool> stop = { false };
    int dummy = 0;
    stop  = false;

    auto test_bare = std::thread ([&stop, &bare_count, &dummy] () {

      while (!stop)
      {
        for (int i = 0; i < 1e6; ++i)
        {
          auto id = i;
          dummy = id;
        }
        ++bare_count;
      }
    });

    std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

    stop = true;
    test_bare.join ();

    BOOST_TEST_MESSAGE ("bare_count=" << bare_count);
  }
  BOOST_CHECK (thread_specific_count < thread_id_count);
  BOOST_CHECK (thread_id_count < thread_local_count);

}


BOOST_AUTO_TEST_CASE (MemoryCopy)
{
  constexpr size_t max = 204800;

  Seconds timeout {2};

  size_t arrIn[max];
  size_t arrOut[max];

  size_t loops_uninitialized_copy_n = 0;
  size_t loops_memcpy = 0;
  size_t loops_memmove = 0;

  const size_t counter_check = 100;


  for (size_t i = 0; i < max; ++i)
  {
    arrIn[i] = i;
  }

  {
    Timer timer;

    timer.start ();

    size_t counter = 0;

    while (true)
    {

      ++counter;

      std::uninitialized_copy_n (arrIn, max, arrOut);

      if (counter == counter_check)
      {
        counter = 0;
        ++loops_uninitialized_copy_n;

        if (to_seconds (timer.elapsed ()) > timeout.count ())
        {
          break;
        }
      }
    }

    timer.stop ();

    BOOST_TEST_MESSAGE ("loops: " << loops_uninitialized_copy_n
                        << " std::uninitialized_copy_n");
  }
  {
    Timer timer;

    timer.start ();

    size_t counter = 0;

    while (true)
    {
      ++counter;

      std::memcpy (arrOut, arrIn, max);

      if (counter == counter_check)
      {
        counter = 0;
        ++loops_memcpy;

        if (to_seconds (timer.elapsed ()) > timeout.count ())
        {
          break;
        }
      }
    }

    timer.stop ();

    BOOST_TEST_MESSAGE ("loops: " << loops_memcpy << " std::memcpy");
  }
  {
    Timer timer;

    timer.start ();

    size_t counter = 0;

    while (true)
    {
      ++counter;

      std::memmove (arrOut, arrIn, max);

      if (counter == counter_check)
      {
        counter = 0;
        ++loops_memmove;

        if (to_seconds (timer.elapsed ()) > timeout.count ())
        {
          break;
        }
      }
    }

    timer.stop ();

    BOOST_TEST_MESSAGE ("loops: " << loops_memmove << " std::memmove");
  }

  BOOST_CHECK (loops_memcpy > (loops_uninitialized_copy_n*5));
  BOOST_CHECK (loops_memmove > (loops_uninitialized_copy_n*5));
}
