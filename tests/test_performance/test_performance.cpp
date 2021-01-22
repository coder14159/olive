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
#include "detail/Utils.h"

#include <boost/circular_buffer.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/log/trivial.hpp>

#include <cstdlib>
#include <ext/numeric>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE SPMCTestPerformance

#include <boost/test/unit_test.hpp>

using namespace std::chrono_literals;
using namespace boost::log::trivial;

namespace ba = boost::accumulators;

using namespace spmc;

const size_t MESSAGE_SIZE = 128;

/*
 * Override time to run each performance test from by modifing the "TIMEOUT"
 * environment variable with a value in seconds.
 *
 * eg TIMEOUT=10 ./build/x86_64/bin/test_performance
 */
TimeDuration get_test_duration ()
{
  Seconds duration (3s);

  if (getenv ("TIMEOUT") != nullptr)
  {
    int seconds = std::atoi (getenv ("TIMEOUT"));

    duration = Seconds (seconds);
  }

  return duration;
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

  auto per_call = (Nanoseconds (now - start) / count);

  BOOST_TEST_MESSAGE ("Duration Clock::now () per call: "
      << nanoseconds_to_pretty (per_call) << " nanoseconds");

  /*
   * Max time for a wall clock call is around 70 ns on a 3.10GHz CPU
   *
   * Note it is possible to implement a faster clock using the rdtsc instruction
   */
  BOOST_CHECK (per_call < Nanoseconds (200));
}

BOOST_AUTO_TEST_CASE (ThroughputCircularBuffers)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto duration = get_test_duration ();

  auto stress_boost_circular_buffer = [&duration] (
    size_t bufferCapacity, size_t messageSize) -> Throughput
  {
    BOOST_CHECK (bufferCapacity > messageSize);

    boost::circular_buffer<uint8_t> buffer (bufferCapacity);

    std::vector<uint8_t> in;
    in.resize (messageSize);
    std::iota (std::begin (in), std::end (in), 1);

    std::vector<uint8_t> out;
    out.reserve (messageSize);

    uint64_t seqNum = 0;

    Throughput throughput;

    Timer timer;

    for (int i = 0; ; ++i)
    {
      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        break;
      }

      // insert data to the buffer
      buffer.insert (buffer.end (), in.begin (), in.end ());

      // copy data out of circular buffer
      std::copy (buffer.begin (), buffer.end (), std::back_inserter (out));

      buffer.erase (buffer.begin (), buffer.end ());

      BOOST_CHECK_EQUAL (in.size (), out.size ());
      BOOST_CHECK_EQUAL (out.size (), messageSize);

      throughput.next (out.size (), ++seqNum);

      out.clear ();

      ++i;
    }

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tboost circular buffer" );

    return throughput;
  };

  auto stress_custom_buffer = [&duration] (
    size_t bufferCapacity, size_t messageSize) -> Throughput
  {
    BOOST_CHECK (bufferCapacity > messageSize);

    Buffer<std::allocator<uint8_t>> buffer (bufferCapacity);

    std::vector<uint8_t> in;
    in.resize (messageSize);
    std::iota (std::begin (in), std::end (in), 1);

    std::vector<uint8_t> out;
    out.reserve (messageSize);

    uint64_t seqNum = 0;

    Throughput throughput;

    Timer timer;

    for (int i = 0; ; ++i)
    {
      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        break;
      }

      // insert data to the buffer
      buffer.push (in);

      // copy data out of custom circular buffer
      buffer.pop (out, in.size ());

      BOOST_CHECK_EQUAL (in.size (), out.size ());
      BOOST_CHECK_EQUAL (out.size (), messageSize);

      throughput.next (out.size (), ++seqNum);

      buffer.clear ();

      out.clear ();

      ++i;
    }

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tcustom circular buffer");

    return throughput;
  };

  auto stress_boost_small_vector = [&duration] (
    size_t bufferCapacity, size_t messageSize) -> Throughput
  {
    BOOST_CHECK (bufferCapacity > messageSize);

    const size_t MAX_SIZE = 1000;

    boost::container::small_vector<uint8_t, MAX_SIZE> buffer;

    std::vector<uint8_t> in;
    in.resize (messageSize);
    std::iota (std::begin (in), std::end (in), 1);

    std::vector<uint8_t> out;
    out.reserve (messageSize);

    uint64_t seqNum = 0;

    Throughput throughput;

    Timer timer;

    for (int i = 0; ; ++i)
    {
      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        break;
      }

      // insert data to the buffer
      buffer.insert (buffer.end (), in.begin (), in.end ());

      // copy data out of vector
      std::copy (buffer.begin (), buffer.end (), std::back_inserter (out));

      BOOST_CHECK_EQUAL (in.size (), out.size ());

      throughput.next (out.size (), ++seqNum);

      buffer.erase (buffer.begin (), buffer.end ());

      out.clear ();

      ++i;
    }

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tboost small_vector");

    return throughput;
  };

  for (size_t bufferCapacity : { 100, 1000, 10000, 100000})
  {
    BOOST_TEST_MESSAGE ("buffer capacity: " << bufferCapacity);

    for (size_t messageSize : { 32, 64, 128, 512, 1024, 2048, 4096 })
    {
      if (messageSize > bufferCapacity)
      {
        BOOST_TEST_MESSAGE ("message_size > buffer_size"
          << " message_size: " << messageSize
          << " buffer_size: " << bufferCapacity);
        continue;
      }
      BOOST_TEST_MESSAGE ("  message size: " << messageSize);

      auto custom_msg_per_sec =
        stress_custom_buffer (bufferCapacity, messageSize).messages_per_sec ();

      auto boost_msg_per_sec =
        stress_boost_circular_buffer (bufferCapacity, messageSize).messages_per_sec ();

      auto small_vector_msg_per_sec =
        stress_boost_small_vector (bufferCapacity, messageSize).messages_per_sec ();

      /*
       * Custom circular buffer is faster than the boost containers
       */
      BOOST_CHECK (custom_msg_per_sec > boost_msg_per_sec);

      /*
       * Small message sizes are faster when using small vector optimisation
       * TODO: maybe add similar to the custom buffer
       */
      if (messageSize > 512)
      {
        BOOST_CHECK (custom_msg_per_sec > small_vector_msg_per_sec);
      }
    }
  }
}

template <class Vector>
void ThroughputOfVector (
    size_t bufferSize, size_t batchSize, const std::string &type)
{
  Buffer<std::allocator<uint8_t>> buffer (bufferSize);
  std::vector<uint8_t> in (batchSize);

  Vector out;

  uint64_t seqNum = 0;

  Throughput throughput;

  auto duration = get_test_duration ();

  Timer timer;

  for (int i = 0; ; ++i)
  {
    if ((i % 1000) == 0 && duration < timer.elapsed ())
    {
      break;
    }
    out.clear ();

    buffer.push (in.data (), in.size ());

    out.resize (in.size ());
    buffer.pop (out, in.size ());

    throughput.next (out.size (), ++seqNum);

    ++i;
  }

  timer.stop ();

  BOOST_TEST_MESSAGE ("    " << type << "\t"
      << throughput_bytes_to_pretty (throughput.bytes (), timer.elapsed ()));
}

BOOST_AUTO_TEST_CASE (VectorThroughput)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto run_test = [] (size_t batchSize) {
    using namespace boost::container;

    size_t containerCapacity = 204800;
    const size_t staticVectorCapacity = 1024;

    BOOST_TEST_MESSAGE ("  container capacity: " << containerCapacity);

    BOOST_TEST_MESSAGE ("  batch size: " << batchSize);

    BOOST_TEST_MESSAGE ("  static_capacity: " << staticVectorCapacity);


    ThroughputOfVector<std::vector<uint8_t>> (
        containerCapacity, batchSize, "std::vector  ");

    ThroughputOfVector<small_vector<uint8_t, staticVectorCapacity>> (
        containerCapacity, batchSize, "small_vector ");

    if (batchSize <= staticVectorCapacity)
    {
      ThroughputOfVector<static_vector<uint8_t, staticVectorCapacity>> (
          containerCapacity, batchSize, "static_vector ");
    }
  };

  run_test (32);
  run_test (64);
  run_test (128);
  run_test (512);
  run_test (1024);
  run_test (2048);
  run_test (4096);

  BOOST_CHECK (true);
}

float latency_percentile_usecs (const PerformanceStats &stats, float percentile)
{
  float nanoseconds = static_cast<int64_t>(ba::p_square_quantile (
                          stats.latency ().summary ().quantiles ()
                               .at (percentile)));
  return (nanoseconds/1000.);
}

void sink_stream_in_single_process (
    size_t            capacity,
    PerformanceStats &stats,
    uint32_t          rate,
    size_t            prefetch)
{
  std::atomic<bool> stop = { false };

  SPMCSinkThread sink (capacity);

  auto producer = std::thread ([&stop, &sink, &rate] () {

    Throttle throttle (rate);

    size_t message_size = MESSAGE_SIZE;

    std::vector<uint8_t> message (message_size, 0);

    std::iota (std::begin (message), std::end (message), 1);

    while (!stop)
    {
      sink.next (message);

      throttle.throttle ();
    }

    sink.stop ();
  });

  while (!producer.joinable ())
  {
    std::this_thread::sleep_for (5ms);
  }

  std::unique_ptr<SPMCStreamThread> stream;

  auto consumer = std::thread ([&stop, &stream, &sink, &stats, &prefetch] () {

    bool allow_message_drops = false;
    /*
     * The stream must be initialised in the thread context in which it is used
     */
    stream = std::make_unique<SPMCStreamThread>
                (sink.queue (), allow_message_drops, prefetch);

    stats.throughput ().summary ().enable (true);
    stats.latency ().summary ().enable (true);

    Header header;

    std::vector<uint8_t> data;

    while (!stop)
    {
      if (stream->next (header, data))
      {
        stats.update (sizeof (Header) + data.size (), header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));
      }
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stream->stop ();
  sink.stop ();

  stop = true;

  consumer.join ();
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

  SPMCSinkThread sink (capacity);

  auto producer = std::thread ([&stop, &sink, &rate] () {

    Throttle throttle (rate);

    POD payload;
    std::iota (reinterpret_cast<uint8_t*> (&payload),
               reinterpret_cast<uint8_t*> (&payload) + sizeof (payload), 1);

    while (!stop)
    {
      sink.next (payload);

      throttle.throttle ();
    }
  });

  while (!producer.joinable ())
  {
    std::this_thread::sleep_for (5ms);
  }

  std::unique_ptr<SPMCStreamThread> stream;

  auto consumer = std::thread ([&stop, &sink, &stream, &prefetch, &stats] () {

    bool allow_message_drops = false;
    /*
     * The stream must be initialised in the thread context in which it is used
     */
    stream = std::make_unique<SPMCStreamThread>
                (sink.queue (), allow_message_drops, prefetch);

    stats.throughput ().summary ().enable (true);
    stats.latency ().summary ().enable (true);

    Header header;

    std::vector<uint8_t> data;

    while (!stop)
    {
      if (stream->next (header, data))
      {
        stats.update (sizeof (Header) + data.size (), header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));
      }
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stream->stop ();
  sink.stop ();

  stop = true;

  consumer.join ();
  producer.join ();
}

BOOST_AUTO_TEST_CASE (PerformanceCoreQueueMultiThread)
{
  std::string log_level = "ERROR";

  if (getenv ("LOG_LEVEL") != nullptr)
  {
    log_level = getenv ("LOG_LEVEL");
  }

  ScopedLogLevel scoped_log_level (log_level);

  const size_t capacity = 8;

  using QueueType = detail::SPMCQueue<std::allocator<uint8_t>>;

  QueueType queue (capacity);

  std::atomic<bool> stop { false };

  Timer timer;

  uint64_t messages_consumed = 0;

  int64_t min = Nanoseconds::max ().count ();
  int64_t max = Nanoseconds::min ().count ();

  bool send { false };

  int64_t total_latency = 0;

  auto consumer = std::thread ([&] () {

    QueueType::ProducerType producer_info;
    QueueType::ConsumerType consumer_info;

    queue.consumer_checks (producer_info, consumer_info);

    int64_t timestamp = 0;

    Timer timer;

    for (int64_t i = 0; ; ++i)
    {
      if (stop)
      {
        timer.stop ();
        break;
      }

      if (queue.pop (timestamp, producer_info, consumer_info))
      {
        int64_t diff = nanoseconds_since_epoch (Clock::now ()) - timestamp;

        total_latency += diff;

        min = (min < diff) ? min : diff;
        max = (max > diff) ? max : diff;

        ++messages_consumed;

        send = true;
      }
    }

    send = false;
  });

  auto producer = std::thread ([&stop, &queue, &send] () {

    while (!stop.load (std::memory_order_relaxed))
    {
      if (queue.push (nanoseconds_since_epoch (Clock::now ())))
      {
        while (!send && !stop)
        {}
      }
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stop = true;

  producer.join ();
  consumer.join ();

  BOOST_CHECK ((messages_consumed/to_seconds (timer.elapsed ())) > 1000);

  BOOST_CHECK (messages_consumed > 1e3);

  // values are in nanoseconds
  BOOST_CHECK (min < 1e3);    // min <  1 us
  BOOST_CHECK (max < 20e6);   // max < 20 ms

  BOOST_TEST_MESSAGE ("Latency");
  BOOST_TEST_MESSAGE ("min:\t" << nanoseconds_to_pretty (min));
  BOOST_TEST_MESSAGE ("avg:\t" << nanoseconds_to_pretty (
                      total_latency / messages_consumed));
  BOOST_TEST_MESSAGE ("max:\t" << nanoseconds_to_pretty (max));
  BOOST_TEST_MESSAGE ("---");

  auto rate = static_cast<uint64_t>((messages_consumed/1e6)
                                      / to_seconds (timer.elapsed ()));

  BOOST_CHECK (rate > 1);   // rate > 1 M ops/sec
  BOOST_TEST_MESSAGE ("Rate:\t" << rate << " M ops/sec");

}

BOOST_AUTO_TEST_CASE (ThroughputSinkStreamMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  std::string log_level = "ERROR";

  if (getenv ("LOG_LEVEL") != nullptr)
  {
    log_level = getenv ("LOG_LEVEL");
  }

  spmc::ScopedLogLevel log (log_level);

  size_t capacity = 20480;
  size_t rate     = 0;
  size_t prefetch = 0;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();

  BOOST_CHECK (throughput.messages_per_sec () > 1e6);

  BOOST_CHECK (throughput.megabytes_per_sec () > 100);

  BOOST_TEST_MESSAGE (throughput.to_string ());
}

BOOST_AUTO_TEST_CASE (LatencySinkStreamMultiThread)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  std::string log_level = "ERROR";

  if (getenv ("LOG_LEVEL") != nullptr)
  {
    log_level = getenv ("LOG_LEVEL");
  }

  spmc::ScopedLogLevel log (log_level);

  size_t capacity = 20480;
  size_t rate     = 1e6;
  size_t prefetch = 0;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();

  BOOST_CHECK (throughput.messages_per_sec () > (rate * 0.9));

  BOOST_CHECK (throughput.megabytes_per_sec () >
    (rate * MESSAGE_SIZE * 0.9 / (1024*1024)));

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (latency_percentile_usecs (stats, 50.) > 0);
  BOOST_CHECK (latency_percentile_usecs (stats, 50.) < 10);

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

  size_t capacity = 204800;
  size_t rate     = 0;
  size_t prefetch = 1024;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > (rate * MESSAGE_SIZE * 0.9));

  BOOST_TEST_MESSAGE (throughput.to_string ());

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

  size_t capacity = 2048000;
  size_t rate     = 1e6;
  size_t prefetch = 1024;

  PerformanceStats stats;

  sink_stream_in_single_process (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > 100);

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (latency_percentile_usecs (stats, 50.) > 0);
  BOOST_CHECK (latency_percentile_usecs (stats, 50.) < 10);

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

  size_t capacity = 20480;
  size_t rate     = 0;
  size_t prefetch = 0;

  PerformanceStats stats;

  sink_stream_in_single_process_pod<char[MESSAGE_SIZE]> (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > 100);

  BOOST_CHECK (latency_percentile_usecs (stats, 50.) > 0);
  BOOST_CHECK (latency_percentile_usecs (stats, 50.) < 100);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
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

  sink_stream_in_single_process_pod<char[MESSAGE_SIZE]> (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > 100);

  BOOST_CHECK (latency_percentile_usecs (stats, 50.) > 0);
  BOOST_CHECK (latency_percentile_usecs (stats, 50.) < 10);

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

  sink_stream_in_single_process_pod<char[MESSAGE_SIZE]> (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();
  /*
   * Prefetch latency can be higher
   */
  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_TEST_MESSAGE (throughput.to_string ());
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

  sink_stream_in_single_process_pod<char[MESSAGE_SIZE]> (capacity, stats, rate, prefetch);

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (latency_percentile_usecs (stats, 50.) < 10);

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
   * Create shared memory in which shared objects can be created
   */
  managed_shared_memory (open_or_create, name.c_str(),
                         capacity + SharedMemory::BOOK_KEEPING);

  SPMCSinkProcess sink (name, name + ":queue", capacity);

  std::atomic<bool> stop = { false };

  auto producer = std::thread ([&stop, &sink, &throttle] () {

    std::vector<uint8_t> message (MESSAGE_SIZE, 0);

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

  auto consumer = std::thread ([&stream, &stats] () {
    Header header;

    std::vector<uint8_t> message (MESSAGE_SIZE, 0);

    while (true)
    {
      if (stream.next (header, message))
      {
        stats.update (header.size+sizeof (Header), header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));
      }
      else
      {
        break;
      }
    }
  });

  // Sleep while producer/consumer threads run
  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stream.stop ();
  sink.stop ();

  stop = true;

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

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);
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

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 100);

  BOOST_CHECK (throughput.megabytes_per_sec () > 200);
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

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > 100);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThreadIdentity)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

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

    /*
     * Test for a short period only otherwise count exceeds max counter value
     */
    std::this_thread::sleep_for (Seconds (1));

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
  size_t loops_uninitialized_copy = 0;
  size_t loops_memcpy = 0;
  size_t loops_memmove = 0;

  const size_t counter_check = 1000;

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

    auto arrEnd = arrIn+max;

    while (true)
    {

      ++counter;

      std::uninitialized_copy (arrIn, arrEnd, arrOut);

      if (counter == counter_check)
      {
        counter = 0;
        ++loops_uninitialized_copy;

        if (to_seconds (timer.elapsed ()) > timeout.count ())
        {
          break;
        }
      }
    }

    timer.stop ();

    BOOST_TEST_MESSAGE ("loops: " << loops_uninitialized_copy
                        << " std::uninitialized_copy");
  }

  BOOST_CHECK (loops_memcpy > (loops_uninitialized_copy*5));
  BOOST_CHECK (loops_memmove > (loops_uninitialized_copy*5));
}

template<typename T>
T remainder (T num, T divisor)
{
    return (num - (divisor * (num / divisor)));
}

BOOST_AUTO_TEST_CASE (Modulus)
{
  BOOST_TEST_MESSAGE (
    "Modulus calculation performance - smaller times are better");

  const int32_t size = 8192;
  const int32_t cycles = std::numeric_limits<int32_t>::max ()/2;

  int32_t modulus = 0;
  int32_t dummy = 0; // used to ensure the calculation isn't optimised away

  std::thread ([&] () {

    std::modulus<int> f;

    Timer timer;

    for (int i = 0; i < cycles; ++i)
    {
      modulus = f ((int)i, size);
      dummy += modulus;
    }

    (void)dummy;
    auto elapsed = timer.elapsed ();

    auto per_call = (static_cast<double>(elapsed.nanoseconds ().count ())
                      / static_cast<double>(cycles));

    BOOST_TEST_MESSAGE (per_call << " ns\tstd::modulus");

    modulus = dummy;
  }).join ();

  modulus = 0;
  dummy = 0;

  std::thread ([&] () {

    Timer timer;

    for (int i = 0; i < cycles; ++i)
    {
      modulus = i % size;
      dummy += modulus;
    }

    auto elapsed = timer.elapsed ();

    auto per_call = (static_cast<double>(elapsed.nanoseconds ().count ())
                      / static_cast<double>(cycles));

    BOOST_TEST_MESSAGE (per_call << " ns\t% operation");

    modulus = dummy;
  }).join ();

  modulus = 0;
  dummy = 0;

  std::thread ([&] () {

    Timer timer;

    for (int i = 0; i < cycles; ++i)
    {
      modulus = MODULUS(i, size);
      dummy += modulus;
    }

    auto elapsed = timer.elapsed ();

    auto per_call = (static_cast<double>(elapsed.nanoseconds ().count ())
                      / static_cast<double>(cycles));

    BOOST_TEST_MESSAGE (per_call << " ns\tremainder macro");

    dummy = modulus;
  }).join ();

  modulus = 0;
  dummy = 0;

  std::thread ([&] () {

    Timer timer;
    for (int i = 0; i < cycles; ++i)
    {
      modulus = remainder<int32_t> (i, size);
      dummy += modulus;
    }

    auto elapsed = timer.elapsed ();

    auto per_call = (static_cast<double>(elapsed.nanoseconds ().count ())
                      / static_cast<double>(cycles));

    BOOST_TEST_MESSAGE (per_call << " ns\tremainder template");

    modulus = dummy;
  }).join ();

  modulus = 0;
  dummy = 0;

  std::thread ([&] () {

    Timer timer;
    for (int i = 0; i < cycles; ++i)
    {
      modulus = (i - (size * (i / size)));
      dummy += modulus;
    }

    auto elapsed = timer.elapsed ();

    auto per_call = (static_cast<double>(elapsed.nanoseconds ().count ())
                      / static_cast<double>(cycles));

    BOOST_TEST_MESSAGE (per_call << " ns\thardwired remainder operation");

    dummy = modulus;
    (void) dummy;
  }).join ();
}

BOOST_AUTO_TEST_CASE (ExpectCondition)
{
  BOOST_CHECK (SPMC_COND_EXPECT (true,  true)  == 1);
  BOOST_CHECK (SPMC_COND_EXPECT (false, false) == 0);

  BOOST_CHECK (SPMC_EXPECT_TRUE (true)  == 1);
  BOOST_CHECK (SPMC_EXPECT_TRUE (false) == 0);

  BOOST_CHECK (SPMC_EXPECT_FALSE (true)  == 1);
  BOOST_CHECK (SPMC_EXPECT_FALSE (false) == 0);

  struct A {
    std::ofstream f;
  };

  auto return_file ([] () -> A
  {
    A a;

    return a;
  });

  A a = return_file ();
}
