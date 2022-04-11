#include "Assert.h"
#include "Buffer.h"
#include "Chrono.h"
#include "CpuBind.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SPMCSource.h"
#include "SPMCSink.h"
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
#include <type_traits>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE SPMCTestPerformance

#include <boost/test/unit_test.hpp>

using namespace std::chrono_literals;
using namespace boost::log::trivial;

namespace ba = boost::accumulators;

using namespace olive;

const size_t PAYLOAD_SIZE = 32;

/*
 * Override time to run each performance test from by modifing the "TIMEOUT"
 * environment variable with a value in seconds.
 *
 * eg TIMEOUT=10 ./build/x86_64/bin/test_performance
 */
TimeDuration get_test_duration ()
{
  Seconds duration (1s);

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

  const int count = 50000000;
  TimePoint now;

  for (int i = 0; i < count; ++i)
  {
    now = Clock::now ();
  }

  auto per_call = (Nanoseconds (now - start) / count);

  /*
   * On the test machine: Intel(R) Core(TM) i5-3450 CPU @ 3.10GHz
   * the duration of a Clock::now () call is ~20 nanoseconds
   */
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

  /*
   * The small_vector implementation has faster throughput but the use of static
   * memory size is less flexible in the shared memory use case
   */
  auto duration = get_test_duration ();

  auto stress_boost_circular_buffer = [&duration] (
    size_t bufferCapacity, size_t messageSize) -> Throughput
  {
    BOOST_CHECK (bufferCapacity >= messageSize);

    boost::circular_buffer<uint8_t> buffer (bufferCapacity);

    std::vector<uint8_t> in;
    in.resize (messageSize);
    std::iota (std::begin (in), std::end (in), 1);

    std::vector<uint8_t> out;
    out.reserve (messageSize);

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

      throughput.next (out.size (), 1);

      out.clear ();
    }
    throughput.stop ();

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tboost circular buffer" );

    return throughput;
  };

  auto stress_custom_buffer = [&duration] (
    size_t bufferCapacity, size_t messageSize) -> Throughput
  {
    BOOST_CHECK (bufferCapacity >= messageSize);

    Buffer<std::allocator<uint8_t>> buffer (bufferCapacity);

    std::vector<uint8_t> in;
    in.resize (messageSize);
    std::iota (std::begin (in), std::end (in), 1);

    std::vector<uint8_t> out;
    out.reserve (messageSize);

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

      throughput.next (out.size (), 1);

      out.clear ();
    }
    throughput.stop ();

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tcustom circular buffer");

    return throughput;
  };

  auto stress_boost_small_vector = [&duration] (
    size_t bufferCapacity, size_t messageSize) -> Throughput
  {
    BOOST_CHECK (bufferCapacity >= messageSize);

    const size_t MAX_SIZE = 100000;

    boost::container::small_vector<uint8_t, MAX_SIZE> buffer;

    std::vector<uint8_t> in;
    in.resize (messageSize);
    std::iota (std::begin (in), std::end (in), 1);

    std::vector<uint8_t> out;
    out.reserve (messageSize);

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

      throughput.next (out.size (), 1);

      buffer.erase (buffer.begin (), buffer.end ());

      out.clear ();
    }

    throughput.stop ();

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tboost small_vector");

    return throughput;
  };

  for (size_t bufferCapacity : { 32, 64, 128, 1024, 2048, 20480})
  {
    BOOST_TEST_MESSAGE ("buffer capacity: " << bufferCapacity);

    for (size_t messageSize : { 32, 64, 128, 512 })
    {
      if (messageSize > bufferCapacity)
      {
        continue;
      }

      BOOST_TEST_MESSAGE ("  message size: " << messageSize);

      auto boost_msg_per_sec =
        stress_boost_circular_buffer (bufferCapacity, messageSize).messages_per_sec ();

      auto custom_msg_per_sec =
        stress_custom_buffer (bufferCapacity, messageSize).messages_per_sec ();

      auto small_vector_msg_per_sec =
        stress_boost_small_vector (bufferCapacity, messageSize).messages_per_sec ();
      /*
       * The custom circular buffer is faster than the boost containers
       */
      BOOST_CHECK (custom_msg_per_sec > 0);
      BOOST_CHECK (boost_msg_per_sec > 0);
      BOOST_CHECK (small_vector_msg_per_sec > 0);
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
      << throughput_messages_to_pretty (throughput.bytes (), timer.elapsed ()));
}

BOOST_AUTO_TEST_CASE (VectorThroughput)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  auto run_test = [] (size_t batchSize) {
    using namespace boost::container;

    size_t containerCapacity = 256;
    const size_t staticVectorCapacity = 256;

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
  run_test (256);

  BOOST_CHECK (true);
}

float latency_percentile_usecs (const PerformanceStats &stats, float percentile)
{
  float nanoseconds = static_cast<int64_t>(ba::p_square_quantile (
                          stats.latency ().summary ().quantiles ()
                               .at (percentile)));
  return (nanoseconds/1000.);
}

template <class PayloadType>
void source_sink_multi_thread (
    size_t            capacity,
    PerformanceStats &stats,
    uint32_t          rate)
{
  BOOST_TEST_MESSAGE ("source_sink_multi_thread 1 source and 2 sinks");

  std::atomic<bool> stop = { false };

  SPMCSourceThread source (capacity);
  SPMCSinkThread sink1 (source.queue ());
  SPMCSinkThread sink2 (source.queue ());

  auto consumer1 = std::thread ([&] () {
    bind_to_cpu (2);

    try
    {
      stats.throughput ().summary ().enable (true);
      stats.latency ().summary ().enable (true);

      Header header;

      std::vector<uint8_t> data;

      while (!stop)
      {
        if (sink1.next (header, data))
        {
          stats.update (sizeof (Header) + data.size (), header.seqNum,
                        TimePoint (Nanoseconds (header.timestamp)));
        }
      }

      stats.stop ();
    }
    catch(const std::exception& e)
    {
      std::cerr << "Consumer 1 exception caught: " << e.what() << '\n';
    }
  });

  auto consumer2 = std::thread ([&] () {
    try
    {
      Header header;

      std::vector<uint8_t> data;

      while (!stop)
      {
        sink2.next (header, data);
      }
    }
    catch(const std::exception& e)
    {
      std::cerr << "Consumer 2 exception caught: " << e.what() << '\n';
    }
  });

  while (!consumer1.joinable () || !consumer2.joinable ())
  {
    std::this_thread::sleep_for (1us);
  }

  auto producer = std::thread ([&stop, &source, &rate] () {
    try
    {
      Throttle throttle (rate);

      PayloadType payload;

      if constexpr (std::is_pod<PayloadType>::value)
      {
        std::iota (reinterpret_cast<uint8_t*> (&payload),
                  reinterpret_cast<uint8_t*> (&payload) + sizeof (payload), 1);
      }
      else if constexpr (std::is_same<std::vector<uint8_t>, PayloadType>::value)
      {
        payload.resize (PAYLOAD_SIZE);
        std::iota (std::begin (payload), std::end (payload), 1);
      }
      else
      {
        throw std::exception ("Invalid PayloadType");
      }

      while (!stop)
      {
        source.next (payload);

        throttle.throttle ();
      }
    }
    catch(const std::exception& e)
    {
      std::cerr << "Producer exception caught: " << e.what() << '\n';
    }
  });

  std::this_thread::sleep_for (5s);

  stats.stop ();

  source.stop ();

  sink1.stop ();
  sink2.stop ();

  stop = true;

  consumer1.join ();
  consumer2.join ();

  producer.join ();
}

BOOST_AUTO_TEST_CASE (ThroughputSourceSinkMultiThreadVectorPayload)
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

  ScopedLogLevel log (log_level);

  size_t capacity = 20480;
  size_t rate     = 0; // throughput throttling is off

  PerformanceStats stats;

  source_sink_multi_thread<std::vector<uint8_t>> (capacity, stats, rate);

  auto &throughput = stats.throughput ().summary ();

  BOOST_CHECK (throughput.messages_per_sec () > 1e3);

  BOOST_CHECK (throughput.megabytes_per_sec () > 1);

  BOOST_TEST_MESSAGE (throughput.to_string ());

}

BOOST_AUTO_TEST_CASE (LatencySourceSinkMultiThreadVectorPayload)
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

  ScopedLogLevel log (log_level);

  size_t capacity = 20480;
  size_t rate     = 100000;

  PerformanceStats stats;

  source_sink_multi_thread<std::vector<uint8_t>> (capacity, stats, rate);

  auto &throughput = stats.throughput ().summary ();

  BOOST_CHECK (throughput.messages_per_sec () > (rate * 0.9));

  BOOST_CHECK (throughput.megabytes_per_sec () >
    (rate * PAYLOAD_SIZE * 0.8 / (1024*1024)));

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (latency_percentile_usecs (stats, 50.) > 0);
  BOOST_CHECK (latency_percentile_usecs (stats, 50.) < 10);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThroughputSourceSinkMultiThreadPODPayload)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  ScopedLogLevel log (error);

  size_t capacity = 150;
  size_t rate     = 0;

  PerformanceStats stats;

  source_sink_multi_thread<char[PAYLOAD_SIZE]> (capacity, stats, rate);

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > 100);
}

BOOST_AUTO_TEST_CASE (LatencySourceSinkMultiThreadPODPayload)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  ScopedLogLevel log (error);

  size_t capacity = 20480;
  size_t rate     = 1e6;

  PerformanceStats stats;

  source_sink_multi_thread<char[PAYLOAD_SIZE]> (capacity, stats, rate);

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > 20);

  BOOST_CHECK (latency_percentile_usecs (stats, 50.) > 0);
  BOOST_CHECK (latency_percentile_usecs (stats, 50.) < 10);

  for (auto &line : stats.latency ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

void source_sink_multi_process (
    size_t            capacity,
    PerformanceStats &stats,
    uint32_t          rate)
{
  using namespace boost;
  using namespace boost::interprocess;

  std::string name = "SourceSinkSharedMemory:Perf";

  struct RemoveSharedMemory
  {
    RemoveSharedMemory (const std::string & name) : name (name)
    { shared_memory_object::remove (name.c_str ()); }

    ~RemoveSharedMemory ()
    { shared_memory_object::remove (name.c_str ()); }

    std::string name;
  } cleanup (name);

  std::atomic<bool> stop = { false };
  /*
   * Create shared memory in which shared objects can be created
   */
  managed_shared_memory (open_or_create, name.c_str(),
                         capacity + SharedMemory::BOOK_KEEPING);

  SPMCSourceProcess source (name, name + ":queue", capacity);

  auto producer = std::thread ([&] () {

    Throttle throttle (rate);

    std::vector<uint8_t> message (PAYLOAD_SIZE, 0);

    std::iota (std::begin (message), std::end (message), 1);

    while (!stop)
    {
      source.next (message);

      throttle.throttle ();
    }
  });

  SPMCSinkProcess sink (name, name + ":queue");

  auto consumer = std::thread ([&sink, &stats] () {
    Header header;

    std::vector<uint8_t> message (PAYLOAD_SIZE, 0);

    while (true)
    {
      if (sink.next (header, message))
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

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  sink.stop ();
  source.stop ();

  stop = true;

  producer.join ();
  consumer.join ();
}

BOOST_AUTO_TEST_CASE (ThroughputSourceSinkMultiProcess)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }
  ScopedLogLevel log (error);

  PerformanceStats stats;

  size_t capacity = 2048000;
  uint32_t rate   = 0;

  source_sink_multi_process (capacity, stats, rate);

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);
}

BOOST_AUTO_TEST_CASE (LatencySourceSinkMultiProcess)
{
  if (getenv ("NOTIMING") != nullptr)
  {
    return;
  }

  ScopedLogLevel log (error);

  PerformanceStats stats;

  size_t capacity = 20480;
  uint32_t rate   = 1e6;

  source_sink_multi_process (capacity, stats, rate);

  auto &throughput = stats.throughput ().summary ();

  BOOST_TEST_MESSAGE (throughput.to_string ());

  BOOST_CHECK (throughput.messages_per_sec () > 1000);

  BOOST_CHECK (throughput.megabytes_per_sec () > 20);

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

  const int64_t size = 8192;
  const int64_t cycles = std::numeric_limits<int64_t>::max ()/1e10;

  int64_t modulus = 0;
  int64_t dummy = 0; // used to ensure the calculation isn't optimised away

  std::thread ([&] () {

    std::modulus<int64_t> f;

    Timer timer;

    for (int64_t i = 0; i < cycles; ++i)
    {
      modulus = f (i, size);
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

    for (int64_t i = 0; i < cycles; ++i)
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

    for (int64_t i = 0; i < cycles; ++i)
    {
      modulus = MODULUS (i, size);
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
    for (int64_t i = 0; i < cycles; ++i)
    {
      modulus = MODULUS_POWER_OF_2(i, size);
      dummy += modulus;
    }

    auto elapsed = timer.elapsed ();

    auto per_call = (static_cast<double>(elapsed.nanoseconds ().count ())
                      / static_cast<double>(cycles));

    BOOST_TEST_MESSAGE (per_call << " ns\tmodulus (divisor power of 2 only)");

    dummy = modulus;
  }).join ();

  modulus = 0;
  dummy = 0;

  std::thread ([&] () {

    Timer timer;

    for (int64_t i = 0; i < cycles; ++i)
    {
      modulus = remainder<int64_t> (i, size);
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
    for (int64_t i = 0; i < cycles; ++i)
    {
      modulus = (i - (size * (i / size)));
      dummy += modulus;
    }

    auto elapsed = timer.elapsed ();

    auto per_call = (static_cast<double>(elapsed.nanoseconds ().count ())
                      / static_cast<double>(cycles));

    BOOST_TEST_MESSAGE (per_call << " ns\thardwired remainder operation");

    dummy = modulus;
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
}
/*
 * Test the core SPMC queue
 */
BOOST_AUTO_TEST_CASE (PerformanceSPMCQueue)
{
  BOOST_TEST_MESSAGE ("PerformanceSPMCQueue");

  std::string log_level = "ERROR";

  if (getenv ("LOG_LEVEL") != nullptr)
  {
    log_level = getenv ("LOG_LEVEL");
  }

  ScopedLogLevel scoped_log_level (log_level);

  const size_t capacity = 20480;

  using QueueType = SPMCQueue<std::allocator<uint8_t>>;

  QueueType queue (capacity);

  detail::ConsumerState consumer;
  queue.register_consumer (consumer);

  bool stop = false;

  Timer timer;

  uint64_t enqueue_blocked = 0;
  uint64_t messages_consumed = 0;

  int64_t min = Nanoseconds::max ().count ();
  int64_t max = Nanoseconds::min ().count ();

  std::atomic<bool> send { false };

  int64_t total_latency = 0;

  auto consumer_thread = std::thread ([&] () {

    int64_t timestamp = 0;

    bind_to_cpu (1);

    Timer timer;

    send = true;

    for (int64_t i = 0; ; ++i)
    {
      if (stop)
      {
        timer.stop ();
        break;
      }

      if (queue.pop (timestamp, consumer))
      {
        const int64_t diff = nanoseconds_since_epoch (Clock::now ()) - timestamp;

        total_latency += diff;

        if (diff < min) min = diff;
        if (diff > max) max = diff;

        ++messages_consumed;
      }
    }

    send = false;
  });

  while (!consumer_thread.joinable ())
  {
    std::this_thread::sleep_for (1us);
  }

  auto producer_thread = std::thread ([&] () {

    bind_to_cpu (2);

    while (!stop)
    {
      const int64_t timestamp = nanoseconds_since_epoch (Clock::now ());

      if (!queue.push (timestamp))
      {
        ++enqueue_blocked;
      }
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  stop = true;

  consumer_thread.join ();
  producer_thread.join ();

  BOOST_CHECK ((messages_consumed/to_seconds (timer.elapsed ())) > 1000);

  BOOST_CHECK (messages_consumed > 1e3);

  // values are in nanoseconds
  BOOST_CHECK (Nanoseconds (min) < 1ms);
  BOOST_CHECK (Nanoseconds (max) < 20ms);

  auto rate = static_cast<uint64_t>((messages_consumed/1e6)
                                      / to_seconds (timer.elapsed ()));

  BOOST_CHECK (rate > 1);   // rate > 1 M ops/sec
  BOOST_TEST_MESSAGE ("Throughput:\t"
      << throughput_messages_to_pretty (messages_consumed, timer.elapsed ()));
  BOOST_TEST_MESSAGE ("Latency");
  BOOST_TEST_MESSAGE ("  min:\t\t" << nanoseconds_to_pretty (min));
  BOOST_TEST_MESSAGE ("  avg:\t\t" << nanoseconds_to_pretty (
                      total_latency / messages_consumed));
  BOOST_TEST_MESSAGE ("  max:\t\t" << nanoseconds_to_pretty (max));

  BOOST_TEST_MESSAGE ("Enqueue blocked: "
    << throughput_messages_to_pretty (enqueue_blocked, timer.elapsed ()));
  BOOST_TEST_MESSAGE (" ");
}

BOOST_AUTO_TEST_CASE (PerformanceSPSCQueue)
{
  BOOST_TEST_MESSAGE ("PerformanceSPSCQueue");

  std::string log_level = "ERROR";

  if (getenv ("LOG_LEVEL") != nullptr)
  {
    log_level = getenv ("LOG_LEVEL");
  }

  ScopedLogLevel scoped_log_level (log_level);

  const size_t capacity = 20480;

  using QueueType = boost::lockfree::spsc_queue<uint8_t>;

  QueueType queue (capacity);

  bool stop = false;

  Timer timer;

  uint64_t enqueue_blocked = 0;
  uint64_t messages_consumed = 0;

  int64_t min = Nanoseconds::max ().count ();
  int64_t max = Nanoseconds::min ().count ();

  std::atomic<bool> send { false };

  int64_t total_latency = 0;

  auto consumer = std::thread ([&] () {

    int64_t timestamp = 0;

    bind_to_cpu (1);

    Timer timer;

    send = true;

    for (int64_t i = 0; ; ++i)
    {
      if (stop)
      {
        timer.stop ();
        break;
      }

      if (queue.read_available () >= sizeof (timestamp) &&
          queue.pop (reinterpret_cast<uint8_t*> (&timestamp), sizeof (timestamp)))
      {
        const int64_t diff = nanoseconds_since_epoch (Clock::now ()) - timestamp;

        total_latency += diff;

        if (diff < min) min = diff;
        if (diff > max) max = diff;

        ++messages_consumed;
      }
    }

    send = false;
  });

  while (!consumer.joinable ())
  {
    std::this_thread::sleep_for (1us);
  }

  auto producer = std::thread ([&] () {

    bind_to_cpu (2);

    while (!stop)
    {
      const int64_t timestamp = nanoseconds_since_epoch (Clock::now ());

      if (queue.write_available () > sizeof (int64_t))
      {
        queue.push (reinterpret_cast <const uint8_t*> (&timestamp),
                    sizeof (int64_t));
      }
      else
      {
        ++enqueue_blocked;
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
  BOOST_CHECK (Nanoseconds (min) < 1ms);
  BOOST_CHECK (Nanoseconds (max) < 20ms);

  auto rate = static_cast<uint64_t>((messages_consumed/1e6)
                                      / to_seconds (timer.elapsed ()));

  BOOST_CHECK (rate > 1);   // rate > 1 M ops/sec
  BOOST_TEST_MESSAGE ("Throughput:\t"
      << throughput_messages_to_pretty (messages_consumed, timer.elapsed ()));
  BOOST_TEST_MESSAGE ("Latency");
  BOOST_TEST_MESSAGE ("  min:\t\t" << nanoseconds_to_pretty (min));
  BOOST_TEST_MESSAGE ("  avg:\t\t" << nanoseconds_to_pretty (
                      total_latency / messages_consumed));
  BOOST_TEST_MESSAGE ("  max:\t\t" << nanoseconds_to_pretty (max));

  BOOST_TEST_MESSAGE ("Enqueue blocked: "
    << throughput_messages_to_pretty (enqueue_blocked, timer.elapsed ()));
  BOOST_TEST_MESSAGE (" ");
}

BOOST_AUTO_TEST_CASE (SmallCircularBuffer)
{
  auto duration = get_test_duration ();

  constexpr size_t capacity = 10;

  auto stress_boost_circular_buffer = [&duration] () -> Throughput
  {
    boost::circular_buffer<int> buffer (capacity);

    Throughput throughput;

    Timer timer;

    for (int i = 0; ; ++i)
    {
      buffer.push_back (1);
      buffer.push_back (2);
      buffer.push_back (3);
      buffer.push_back (4);

      buffer.pop_front ();
      buffer.pop_front ();
      buffer.pop_front ();
      buffer.pop_front ();

      throughput.next (sizeof (int), 4);

      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        break;
      }
    }
    throughput.stop ();

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tboost circular buffer" );

    return throughput;
  };

  auto stress_local_dynamic_circular_buffer = [&duration] () -> Throughput
  {
    /*
     * Buffer is out-performaned by the boost circular buffer for small sizes
     * but see test ThroughputCircularBuffers for tests with a larger capacities
     * in which Buffer is clearly faster.
     */
    Buffer<std::allocator<int>> buffer (capacity);

    Throughput throughput;

    Timer timer;

    int out;

    for (int i = 0; ; ++i)
    {
      buffer.push (1);
      buffer.push (2);
      buffer.push (3);
      buffer.push (4);

      buffer.pop (out);
      buffer.pop (out);
      buffer.pop (out);
      buffer.pop (out);

      throughput.next (sizeof (int), 4);

      if ((i % 1000) == 0 && duration < timer.elapsed ())
      {
        break;
      }
    }

    throughput.stop ();

    timer.stop ();

    BOOST_TEST_MESSAGE (throughput.to_string () << "\tlocal circular buffer" );

    return throughput;
  };
  auto throughput_boost = stress_boost_circular_buffer ();
  auto throughput_local_circular_buffer = stress_local_dynamic_circular_buffer ();

  BOOST_CHECK (throughput_boost.megabytes_per_sec () >
                  throughput_local_circular_buffer.megabytes_per_sec ());
}
