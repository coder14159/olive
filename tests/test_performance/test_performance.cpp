#include "Buffer.h"
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

#include <ext/numeric>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE SPMCTestPerformance

#define SPMC_DEBUG_ASSERT

#include <boost/test/unit_test.hpp>

using namespace std::chrono_literals;
using namespace boost::log::trivial;

using namespace spmc;

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

BOOST_AUTO_TEST_CASE (PerformanceOfCircularBuffers)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  std::vector<uint8_t> in (1024);
  std::iota (in.begin (), in.end (), 1);

  TimeDuration duration (Nanoseconds (Seconds (5)));

  size_t size = 204800;

  /*
   * Throughput in GB/s
   */
  double bufferThroughput = 0.;
  double boostThroughput  = 0.;

  {
    Buffer<std::allocator<uint8_t>> buffer (size);
    std::vector<uint8_t> out;

    uint64_t bytes = 0;

    Timer timer;

    for (int i = 0; ; ++i)
    {
      if ((i % 1000000) == 0 && duration < timer.elapsed ())
      {
        timer.elapsed ().pretty ();
        break;
      }

      buffer.push (in.data (), in.size ());

      buffer.pop (out, in.size ());
      bytes += out.size ();

      buffer.push (in.data (), in.size ());
      buffer.push (in.data (), in.size ());

      buffer.pop (out, in.size () * 2);
      bytes += out.size ();

      ++i;
    }

    timer.stop ();

    bufferThroughput = static_cast<double> (bytes) / (1024.*1024.*1024.)
                    / to_seconds_floating_point (timer.elapsed ());

    BOOST_TEST_MESSAGE ("Custom Buffer throughput\t\t"
                        << std::fixed << std::setprecision (3)
                        << bufferThroughput << " GB/s");
  }

  {
    boost::circular_buffer<uint8_t> buffer (size);
    std::vector<uint8_t> out;
    out.reserve (size);

    Timer timer;
    uint64_t i = 0;
    uint64_t bytes = 0;
    while (true)
    {
      // check time more often as circular buffer is slow
      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        break;
      }

      buffer.insert (buffer.end (), in.begin (), in.end ());

      out.clear ();
      // is there a better way to copy from and then remove data from
      // a boost::circular_buffer?
      std::copy (buffer.begin (), buffer.end (), std::back_inserter(out));

      buffer.erase (buffer.begin (), buffer.begin () + in.size ());
      bytes += out.size ();

      buffer.insert (buffer.end (), in.begin (), in.end ());
      buffer.insert (buffer.end (), in.begin (), in.end ());
      bytes += out.size ();

      out.clear ();

      std::copy (buffer.begin (), buffer.end (), std::back_inserter(out));
      buffer.erase (buffer.begin (), buffer.begin () + in.size () * 2);

      BOOST_CHECK (out.size () == in.size () * 2);

      ++i;
    }

    timer.stop ();

    boostThroughput = static_cast<double> (bytes) / (1024.*1024.*1024.)
                    / to_seconds_floating_point (timer.elapsed ());

    BOOST_TEST_MESSAGE ("boost::circular_buffer throughput\t"
                        << std::fixed << std::setprecision (3)
                        << boostThroughput << " GB/s");
    /*
     * Expect the throughput of the local custom circular buffer to be faster
     * than boost::circular_buffer by roughly x100 for my use case.
     */
    BOOST_CHECK (bufferThroughput > (boostThroughput*10));
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

  TimeDuration duration (Seconds (2));

  Timer timer;

  while (true)
  {
    if ((i % 1000000) == 0 && duration < timer.elapsed ())
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
                / to_seconds_floating_point (timer.elapsed ());
};

BOOST_AUTO_TEST_CASE (VectorPerformance)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto run_test = [](size_t batchSize) {
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
  run_test (4092);

  BOOST_CHECK (true);
}

#if 0
void async_queue_performance (
  size_t            capacity,
  PerformanceStats &stats,
  uint32_t          rate)
{
  constexpr size_t size = 128;
  struct Data
  {
    Header header;
    char   data[size];
  };

  auto duration = seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = seconds (atoi (getenv ("TIMEOUT")));
  }

  Async::Queue<Data> queue (capacity/sizeof (Data));
  Async::Flag stop;

  Throttle throttle (rate);

  auto producer = std::thread ([&stop, &queue, &throttle] () {
    /*
     * Make size of data reasonably comparable to PerformanceOfSinkStreamPOD by
     * sending data of size header + payload
     */
    uint64_t seqNum = 0;

    while (true)
    {
      Data data;

      throttle.throttle ();

      data.header.timestamp = Time::now ().serialise ();

      data.header.seqNum = ++seqNum;
      data.header.size   = size;

      if (Async::wait (stop.is_set (),
                       queue.send (std::move (data))) == 0)
      {
        break;
      }
    }
  });

  sleep_for (milliseconds (5));

  auto consumer = std::thread ([&stop, &queue, &stats] () {

    uint64_t seqNum = 0;
    while (true)
    {
      Data data;

      if (Async::wait (stop.is_set (), queue.receive (data)) == 0)
      {
        break;
      }

      stats.update (data.header.size+sizeof (Header), data.header.seqNum,
                    Time::deserialise (data.header.timestamp));
    }
  });


  sleep_for (duration);

  stop.set ();

  consumer.join ();
  producer.join ();
}

BOOST_AUTO_TEST_CASE (ThroughputPerformanceOfAsync)
{
  /*
   * Use the performance of the async queue as a baseline.
   *
   * Under high throughput it is essentially the performance of the underlying
   * boost::spsc_queue
   */

  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = seconds (atoi (getenv ("TIMEOUT")));
  }

  auto level = ipc::logger ().level ();
  ipc::logger ().set_level (ERROR);

  BOOST_SCOPE_EXIT (&level) {
    ipc::logger ().set_level (level);
  }BOOST_SCOPE_EXIT_END;

  /*
   * Give the queue the same number of elements as the SPMC queue.
   */

  size_t capacity = 2048000;
  uint32_t rate   = 0;

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);

  async_queue_performance (capacity, stats, rate);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1.0e6) << " M messages/sec");

}

BOOST_AUTO_TEST_CASE (LatencyPerformanceOfAsync)
{
  /*
   * Use the performance of the async queue as a baseline.
   *
   * Under high throughput it is essentially the performance of the underlying
   * boost::spsc_queue
   */

  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = seconds (atoi (getenv ("TIMEOUT")));
  }

  auto level = ipc::logger ().level ();
  ipc::logger ().set_level (ERROR);

  BOOST_SCOPE_EXIT (&level) {
    ipc::logger ().set_level (level);
  }BOOST_SCOPE_EXIT_END;

  size_t capacity = 20480;

  uint32_t rate  = 1e6;

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);
  stats.latency ().summary ().enable (true);

  async_queue_performance (capacity, stats, rate);

  auto megabytes_per_sec = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (megabytes_per_sec > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (megabytes_per_sec/1024.) << " GB/sec");

  auto messages_per_sec = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (messages_per_sec/1.0e6) << " M messages/sec");

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

#endif // Async tests not available


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

  std::this_thread::sleep_for (Seconds (2));

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

  std::this_thread::sleep_for (Milliseconds (5));

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

  std::this_thread::sleep_for (Seconds (2));

  stop = true;

  stream.stop ();
  consumer.join ();

  sink.stop ();
  producer.join ();
}

BOOST_AUTO_TEST_CASE (ThroughputPerformanceOfSinkStream)
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
  stats.throughput ().summary ().enable (true);

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << (rate/1.0e6) << " M messages/sec");
}

BOOST_AUTO_TEST_CASE (LatencyPerformanceOfSinkStream)
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
  stats.throughput ().summary ().enable (true);
  stats.latency ().summary ().enable (true);

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << (rate/1.0e6) << " M messages/sec");
  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThroughputPerformanceOfSinkStreamWithPrefetch)
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
  stats.throughput ().summary ().enable (true);

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 2e6);
}

BOOST_AUTO_TEST_CASE (LatencyPerformanceOfSinkStreamWithPrefetch)
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
  stats.throughput ().summary ().enable (true);
  stats.latency ().summary ().enable (true);

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate " << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 500000);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThroughputPerformanceOfSinkStreamPOD)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = Seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = Seconds (atoi (getenv ("TIMEOUT")));
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 2048000;
  size_t rate     = 0;
  size_t prefetch = 1024;

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 1e6);
}

BOOST_AUTO_TEST_CASE (LatencyPerformanceOfSinkStreamPOD)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = Seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = Seconds (atoi (getenv ("TIMEOUT")));
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 20480;
  size_t rate     = 1e6;
  size_t prefetch = 0;

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);
  stats.latency ().summary ().enable (true);

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 500000);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThroughputPerformanceOfSinkStreamPODWithPrefetch)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = Seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = Seconds (atoi (getenv ("TIMEOUT")));
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 2048000;
  size_t rate     = 0;
  size_t prefetch = 1024;

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (rate/1.0e6) << " M messages/sec");

  BOOST_CHECK (rate > 1e6);
}

BOOST_AUTO_TEST_CASE (LatencyPerformanceOfSinkStreamPODWithPrefetch)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = Seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = Seconds (atoi (getenv ("TIMEOUT")));
  }

  spmc::ScopedLogLevel log (error);

  size_t capacity = 20480;
  size_t rate     = 1e6;
  size_t prefetch = 1024;

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);
  stats.latency ().summary ().enable (true);

  sink_stream_in_single_process_pod<char[128]> (capacity, stats, rate, prefetch);

  auto throughput = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (throughput > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (throughput/1024.) << " GB/sec");

  rate = stats.throughput ().summary ()
                            .messages_per_sec (Time::now ());

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

  auto duration = Seconds (2);

  if (getenv ("TIMEOUT") != nullptr)
  {
    duration = Seconds (atoi (getenv ("TIMEOUT")));
  }

  std::string name = "SinkStreamInSharedMemory:Perf";

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

  std::this_thread::sleep_for (Seconds (2));

  stream.stop ();
  sink.stop ();

  stop = true;

  // consume messages from the shared memory queue
  producer.join ();
  consumer.join ();

}

BOOST_AUTO_TEST_CASE (ThroughputPerformanceSinkStreamInSharedMemory)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);

  size_t capacity = 2048000;
  uint32_t rate   = 0;
  size_t prefetch = 0;

  sink_stream_in_shared_memory (capacity, stats, rate, prefetch);

  auto megabytes_per_sec = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (megabytes_per_sec > 500);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (megabytes_per_sec/1024.) << " GB/sec");

  auto messages_per_sec = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (messages_per_sec/1.0e6) << " M messages/sec");
}

BOOST_AUTO_TEST_CASE (ThroughputPerformanceSinkStreamInSharedMemoryPrefetch)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);

  size_t capacity = 2048000;
  uint32_t rate   = 0;
  size_t prefetch = 10240;

  sink_stream_in_shared_memory (capacity, stats, rate, prefetch);

  auto megabytes_per_sec = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (megabytes_per_sec > 1000);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (megabytes_per_sec/1024.) << " GB/sec");

  auto messages_per_sec = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Time::now ());

  BOOST_TEST_MESSAGE ("rate\t\t" << std::fixed << std::setprecision (3)
                      << (messages_per_sec/1.0e6) << " M messages/sec");
}

BOOST_AUTO_TEST_CASE (LatencyPerformanceSinkStreamInSharedMemory)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  spmc::ScopedLogLevel log (error);

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);
  stats.latency ().summary ().enable (true);

  size_t capacity = 20480;
  uint32_t rate   = 1e6;
  size_t prefetch = 0;

  sink_stream_in_shared_memory (capacity, stats, rate, prefetch);

  auto megabytes_per_sec = stats.throughput ()
                         .summary ()
                         .megabytes_per_sec (Time::now ());

  BOOST_CHECK (megabytes_per_sec > 100);

  BOOST_TEST_MESSAGE ("throughput\t" << std::setprecision (3)
                      << (megabytes_per_sec/1024.) << " GB/sec");

  auto messages_per_sec = stats.throughput ()
                         .summary ()
                         .messages_per_sec (Time::now ());

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

    std::this_thread::sleep_for (Seconds (2));
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

    std::this_thread::sleep_for (Seconds (2));
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

    std::this_thread::sleep_for (Seconds (2));
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

    std::this_thread::sleep_for (Seconds (2));
    stop = true;
    test_bare.join ();

    BOOST_TEST_MESSAGE ("bare_count=" << bare_count);
  }
  BOOST_CHECK ((2*thread_specific_count) < thread_id_count);
  BOOST_CHECK ((2*thread_id_count) < thread_local_count);

}

