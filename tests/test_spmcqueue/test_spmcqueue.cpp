#include "Buffer.h"
#include "Chrono.h"
#include "LatencyStats.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SPMCQueue.h"
#include "SPMCSource.h"
#include "SPMCSink.h"
#include "Throttle.h"
#include "detail/SharedMemory.h"

#include <boost/core/noncopyable.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/scope_exit.hpp>

#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE SPMCQueueTests
#include <boost/test/unit_test.hpp>

using namespace olive;
using namespace boost::log::trivial;
using namespace std::literals::chrono_literals;

/*
 * Override time to run each performance test from the environment variable
 * "TIMEOUT" which should contain a value in seconds.
 */
TimeDuration get_test_duration ()
{
  Nanoseconds duration (1s);

  if (getenv ("TIMEOUT") != nullptr)
  {
    double seconds = std::atof (getenv ("TIMEOUT"));

    int64_t ns = seconds * 1e9;

    duration = Nanoseconds (ns);
  }

  return duration;
}

BOOST_AUTO_TEST_CASE (BasicBufferTests)
{
  ScopedLogLevel log (error);

  { // Add max data size
    Buffer<std::allocator<uint8_t>> buffer (5);
    BOOST_CHECK (buffer.empty ());
    BOOST_CHECK_EQUAL (buffer.capacity (), 5);
    BOOST_CHECK_EQUAL (buffer.size (), 0);

    std::vector<uint8_t> out;
    std::vector<uint8_t> in (5);
    std::iota (in.begin (), in.end (), 0);

    buffer.push (in);
    buffer.pop (out, in.size ());
    BOOST_CHECK (in == out);

    in = {1, 2, 3};
    buffer.push (in);
    buffer.pop (out, in.size ());
    BOOST_CHECK (in == out);
  }
  { // Add max data size
    Buffer<std::allocator<uint8_t>> buffer (5);
    std::vector<uint8_t> out;
    std::vector<uint8_t> in (5);
    std::iota (in.begin (), in.end (), 0);
    in[0] = 1;
    buffer.push (in);
    buffer.pop (out, in.size ());
    BOOST_CHECK (in == out);

    in[0] = 2;
    buffer.push (in);
    buffer.pop (out, in.size ());
    BOOST_CHECK (in == out);

    in[0] = 3;
    buffer.push (in);
    buffer.pop (out, in.size ());
    BOOST_CHECK (in == out);
  }
  { // Wrap the buffer
    Buffer<std::allocator<uint8_t>> buffer (100);

    std::vector<uint8_t> out;
    std::vector<uint8_t> in (30);
    std::iota (in.begin (), in.end (), 0);

    for (int i = 0; i < 10; ++i)
    {
      buffer.push (in.data (), in.size ());
      buffer.pop (out, in.size ());

      BOOST_CHECK_EQUAL (out.size (), in.size ());
      BOOST_CHECK (in == out);
    }
  }
  { // Wrap the buffer size a multiple of data
    Buffer<std::allocator<uint8_t>> buffer (10);

    std::vector<uint8_t> out;
    std::vector<uint8_t> in (5);
    std::iota (in.begin (), in.end (), 0);

    for (int i = 0; i < 3; ++i)
    {
      in[0] = i;
      buffer.push (in.data (), in.size ());
      buffer.pop (out, in.size ());

      BOOST_CHECK_EQUAL (out.size (), in.size ());
      BOOST_CHECK (in == out);
    }
  }
}

BOOST_AUTO_TEST_CASE (BufferPopStruct)
{
  ScopedLogLevel log (error);

  Buffer<std::allocator<uint8_t>> buffer (100);

  struct Data
  {
    int  a = 101;
    char b = 'b';
  };

  Data in = { 5, 'z' };
  Data out;

  buffer.push (in);
  buffer.pop (out);

  BOOST_CHECK_EQUAL (in.a, out.a);
  BOOST_CHECK_EQUAL (in.b, out.b);
}

BOOST_AUTO_TEST_CASE (BufferConsumesFromSPSCQueue)
{
  ScopedLogLevel log (error);

  Buffer<std::allocator<uint8_t>> buffer (7);

  BOOST_CHECK_EQUAL (buffer.size (),     0);
  BOOST_CHECK_EQUAL (buffer.empty (),    true);
  BOOST_CHECK_EQUAL (buffer.capacity (), 7);

  std::vector<uint8_t> data;
  data.resize (4);
  std::iota (std::begin (data), std::end (data), 1);

  boost::lockfree::spsc_queue<uint8_t> queue (10);

  BOOST_CHECK_EQUAL (data.size (), 4);
  BOOST_CHECK_EQUAL (queue.read_available (), 0);

  // populate a queue with 2 packets of data
  queue.push (data.begin (), data.end ());
  BOOST_CHECK_EQUAL (queue.read_available (), 4);

  queue.push (data.begin (), data.end ());
  BOOST_CHECK_EQUAL (queue.read_available (), 8);

  // fill the buffer with 7 bytes of data from the queue
  BOOST_CHECK_EQUAL (buffer.push (queue), true);

  BOOST_CHECK_EQUAL (buffer.size (), 7);
  BOOST_CHECK_EQUAL (queue.read_available (), 1);

  // no more room in the buffer
  BOOST_CHECK_EQUAL (buffer.push (queue), false);

  queue.push (data.begin (), data.end ());
  BOOST_CHECK_EQUAL (queue.read_available (), 5);

  // read some data from the buffer
  std::vector<uint8_t> out;

  BOOST_CHECK_EQUAL (buffer.pop (out, 4), true);

  queue.push (data.begin (), data.end ());
  BOOST_CHECK_EQUAL (queue.read_available (), 9);


  BOOST_CHECK_EQUAL (buffer.push (queue), true);
  BOOST_CHECK_EQUAL (buffer.size (), 7);

  BOOST_CHECK_EQUAL (queue.read_available (), 5);
}

BOOST_AUTO_TEST_CASE (SPMCQueueCapacityCheck)
{
  ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (120);

  detail::ConsumerState consumer;

  queue.register_consumer (consumer);

  const size_t data_size = 8;
  std::vector<uint8_t> in (data_size), empty;
  std::iota (std::begin (in), std::end (in), 1);

  empty.assign (in.size (), 0);
  auto out = empty;

  Header headerIn, headerOut;
  headerIn.seqNum = 1;
  headerIn.version = 1;
  headerIn.type = STANDARD_MESSAGE_TYPE;
  headerIn.size = in.size ();
  headerIn.timestamp = 123456;

  auto push = [&] (const std::string &text) {

    if (queue.push (headerIn, in))
    {
      BOOST_TEST_MESSAGE (text << " seqNum: " << headerIn.seqNum);
      ++headerIn.seqNum;
      return true;
    }

    BOOST_TEST_MESSAGE (text << " queue full");
    return false;
  };

  auto pop = [&] (const std::string &text) {
    out = empty;
    headerOut = Header ();
    if (queue.pop (headerOut, out, consumer))
    {
      BOOST_TEST_MESSAGE (text << "  seqNum: " << headerOut.seqNum);
    }
    else
    {
      BOOST_TEST_MESSAGE (text << "  queue empty");
    }
  };

  bool ret = false;
  ret = push ("1)  push:"); BOOST_CHECK (ret);
  ret = push ("2)  push:"); BOOST_CHECK (ret);
  ret = push ("3)  push:"); BOOST_CHECK (ret);
  ret = push ("4)  push:"); BOOST_CHECK (!ret);

  pop ("5)  pop:");
  BOOST_CHECK (in == out);
  BOOST_CHECK (headerOut.seqNum == 1);
  out = empty;

  pop ("6)  pop:");
  BOOST_CHECK (in == out);
  BOOST_CHECK (headerOut.seqNum == 2);
  out = empty;

  pop ("7)  pop:");
  BOOST_CHECK (in == out);
  BOOST_CHECK (headerOut.seqNum == 3);
  out = empty;

  pop ("8)  pop:");
  BOOST_CHECK (out == empty);

  pop ("9)  pop:");
  BOOST_CHECK (out == empty);

  push ("10) push:");
  push ("11) push:");
  push ("12) push:");

  out = empty;
  pop ("13) pop:");
  BOOST_CHECK (in == out);
  BOOST_CHECK (headerOut.seqNum == 4);

  queue.unregister_consumer (consumer);
}

BOOST_AUTO_TEST_CASE (SPMCQueueBasicTest)
{
  ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (100);

  detail::ConsumerState consumer;

  queue.register_consumer (consumer);

  std::vector<uint8_t> payloadIn (8);
  std::iota (std::begin (payloadIn), std::end (payloadIn), 1);

  Header headerIn;
  headerIn.version = 1;
  headerIn.type    = STANDARD_MESSAGE_TYPE;
  headerIn.size    = payloadIn.size ();
  headerIn.seqNum  = 1;

  bool success = false;
  for (int i = 0; i < 5; ++i)
  {
    headerIn.timestamp = nanoseconds_since_epoch (Clock::now ());

    success = queue.push (headerIn, payloadIn);

    if (headerIn.seqNum < 3)
    {
      BOOST_CHECK_EQUAL (success, true);
      ++headerIn.seqNum;
    }
    else
      BOOST_CHECK_EQUAL (success, false);
  }

  BOOST_CHECK_EQUAL (queue.read_available (consumer), 80);

  std::vector<uint8_t> payloadOut;
  Header headerOut;

  success = queue.pop (headerOut, payloadOut, consumer);

  BOOST_CHECK_EQUAL (success, true);
  BOOST_CHECK_EQUAL (headerOut.version, headerIn.version);
  BOOST_CHECK_EQUAL (headerOut.type,    headerIn.type);
  BOOST_CHECK_EQUAL (headerOut.seqNum,  1);
  BOOST_CHECK_EQUAL (queue.read_available (consumer),  40);

  success = queue.pop (headerOut, payloadOut, consumer);

  BOOST_CHECK_EQUAL (success, true);
  BOOST_CHECK_EQUAL (headerOut.version, headerIn.version);
  BOOST_CHECK_EQUAL (headerOut.type,    headerIn.type);
  BOOST_CHECK_EQUAL (headerOut.seqNum,  2);
  BOOST_CHECK_EQUAL (queue.read_available (consumer),  0);

  success = queue.pop (headerOut, payloadOut, consumer);
  BOOST_CHECK_EQUAL (success, false);
  BOOST_CHECK_EQUAL (queue.read_available (consumer),  0);

  success = queue.pop (headerOut, payloadOut, consumer);
  BOOST_CHECK_EQUAL (success, false);
  BOOST_CHECK_EQUAL (queue.read_available (consumer),  0);

  // test pushing enough data to wrap the buffer a few times
  int start = headerOut.seqNum + 1;
  int end   = start + 20;
  for (int i = start; i < end; ++i)
  {
    {
      headerIn.timestamp = nanoseconds_since_epoch (Clock::now ());

      success = queue.push (headerIn, payloadIn);

      BOOST_CHECK_EQUAL (success, true);
      ++headerIn.seqNum;
    }
    {
      std::vector<uint8_t> payload;

      success = queue.pop (headerOut, payload, consumer);

      BOOST_CHECK_EQUAL (success, true);
      BOOST_CHECK_EQUAL (headerOut.version, headerIn.version);
      BOOST_CHECK_EQUAL (headerOut.type,    headerIn.type);
      BOOST_CHECK_EQUAL (headerOut.seqNum,  i);

      BOOST_CHECK_EQUAL (payload.size (), payloadIn.size ());
      BOOST_CHECK (payload == payloadIn);
    }
  }
}

BOOST_AUTO_TEST_CASE (SPMCQueuePushPod)
{
  ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (100);

  detail::ConsumerState consumer;

  queue.register_consumer (consumer);

  struct Payload
  {
    int  i;
    char c;
  };

  Payload payloadIn = { 100, 'z' };

  for (uint64_t i = 1; i < 500; ++i)
  {
    auto time = nanoseconds_since_epoch (Clock::now ());
    Header headerIn  = { 1, 2, sizeof (Payload), i, time };

    bool success = queue.push (headerIn, payloadIn);
    BOOST_CHECK (success);

    Header headerOut;
    std::vector<uint8_t> data;

    success = queue.pop (headerOut, data, consumer);
    BOOST_CHECK (success);
    BOOST_CHECK_EQUAL (headerIn.version, headerOut.version);
    BOOST_CHECK_EQUAL (headerIn.timestamp, headerOut.timestamp);

    Payload *payloadOut = reinterpret_cast<Payload*> (data.data ());

    BOOST_CHECK_EQUAL (payloadIn.i, payloadOut->i);
    BOOST_CHECK_EQUAL (payloadIn.c, payloadOut->c);
  }
}

BOOST_AUTO_TEST_CASE (SPMCQueuePushVector)
{
  ScopedLogLevel log (error);

  const size_t capacity = 100;
  SPMCQueue<std::allocator<uint8_t>> queue (capacity);

  detail::ConsumerState consumer;

  queue.register_consumer (consumer);

  std::vector<uint8_t> payloadIn;
  payloadIn.resize (capacity - sizeof (Header));

  uint64_t seqNum = 0;
  int64_t time = nanoseconds_since_epoch (Clock::now ());
  Header headerIn  = { 1, 2, payloadIn.size (), ++seqNum, time };

  bool success = queue.push (headerIn, payloadIn);
  BOOST_CHECK (success);

  Header headerOut;
  std::vector<uint8_t> payloadOut;

  success = queue.pop (headerOut, payloadOut, consumer);
  BOOST_CHECK (success);
  BOOST_CHECK_EQUAL (headerIn.version, headerOut.version);
  BOOST_CHECK_EQUAL (headerIn.timestamp, headerOut.timestamp);

  BOOST_CHECK (payloadIn == payloadOut);
}

BOOST_AUTO_TEST_CASE (SPSCQueuePushVector)
{
  ScopedLogLevel log (error);

  boost::lockfree::spsc_queue<uint8_t> queue (100);
  size_t payloadSize = 40;
  const std::vector<uint8_t> empty (payloadSize, 0);

  std::vector<uint8_t> payloadIn = empty;
  std::vector<uint8_t> payloadOut = empty;
  std::iota (std::begin (payloadIn), std::end (payloadIn), 1);

  for (uint64_t i = 1; i < 10; ++i)
  {
    bool success = queue.push (payloadIn.data (), payloadIn.size ());
    BOOST_CHECK (success);

    success = queue.pop (payloadOut.data (), payloadSize);
    BOOST_CHECK (success);

    BOOST_CHECK (payloadIn == payloadOut);

    payloadOut = empty;
  }
}


#if PREFETCH_FIXED

BOOST_AUTO_TEST_CASE (ConsumerPrefetchSmallerThanMessageSize)
{
  ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (1024);
  queue.register_consumer ();

  size_t payloadSize = 128;

  Header headerIn;
  headerIn.version = 1;
  headerIn.type = 2;
  headerIn.size = payloadSize;
  headerIn.seqNum = 1;
  headerIn.timestamp = nanoseconds_since_epoch (Clock::now ());

  std::vector<uint8_t> payloadIn (payloadSize);
  std::iota (std::begin (payloadIn), std::end (payloadIn), 1);

  BOOST_CHECK (queue.push (headerIn, payloadIn)); ++headerIn.seqNum;
  bool success = queue.push (headerIn, payloadIn);  ++headerIn.seqNum;
  BOOST_CHECK (success);

  Header headerOut;
  std::vector<uint8_t> payloadOut;
  success = queue.pop (headerOut, payloadOut);
  BOOST_CHECK (success);
  BOOST_CHECK_EQUAL (headerOut.seqNum, 1);
  BOOST_CHECK (payloadIn == payloadOut);
  payloadOut.clear ();

  BOOST_CHECK (!queue.cache_enabled ());
  /*
   * The next call to pop should fail as the cache size is too small.
   *
   * The cache should be disabled at this point and consuming continues without
   * the use of the cache.
   */
  queue.resize_cache (50);
  BOOST_CHECK (queue.cache_enabled ());
  success = queue.pop (headerOut, payloadOut);

  BOOST_CHECK (success);
  BOOST_CHECK (!queue.cache_enabled ());

  BOOST_CHECK_EQUAL (headerOut.seqNum, 2);
  BOOST_CHECK (payloadIn == payloadOut);
  payloadOut.clear ();

  BOOST_CHECK (!queue.pop (headerOut, payloadOut));

  success = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (success);

  success = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (success);

  BOOST_CHECK (queue.pop (headerOut, payloadOut));
  BOOST_CHECK_EQUAL (headerOut.seqNum, 3);
  BOOST_CHECK (payloadIn == payloadOut);

  success = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (success);
  success = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (success);
  success = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (success);

  success = queue.pop (headerOut, payloadOut);
  BOOST_CHECK (success);
  BOOST_CHECK_EQUAL (headerOut.seqNum, 4);
  BOOST_CHECK (payloadIn == payloadOut);
}
#endif

BOOST_AUTO_TEST_CASE (SlowConsumerNoMessageDrops)
{
  ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (150);

  detail::ConsumerState consumer;
  queue.register_consumer (consumer);

  const size_t payloadSize = 8;

  Header headerProducer;
  headerProducer.version = 1;
  headerProducer.type = STANDARD_MESSAGE_TYPE;
  headerProducer.size = payloadSize;
  headerProducer.seqNum = 1;
  headerProducer.timestamp = nanoseconds_since_epoch (Clock::now ());

  std::vector<uint8_t> payloadProducer (payloadSize, 0);
  std::iota (std::begin (payloadProducer), std::end (payloadProducer), 1);

  Header header;
  std::vector<uint8_t> payload;
  /*
   * The first call to pop initialises the consumer and ensures it is ready to
   * start consuming from the next message to arrive
   */
  BOOST_CHECK_EQUAL (queue.pop (header, payload, consumer), false);

  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true); // commit=40
  BOOST_CHECK_EQUAL (queue.pop (header, payload, consumer), true); // consumed=40

  BOOST_CHECK_EQUAL (header.version,    headerProducer.version);
  BOOST_CHECK_EQUAL (header.type,       headerProducer.type);
  BOOST_CHECK_EQUAL (header.timestamp,  headerProducer.timestamp);
  BOOST_CHECK_EQUAL (payload.size (), payloadSize);
  BOOST_CHECK (payload == payloadProducer);

  // send enough data to wrap the buffer and exert back pressure on the producer
  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true); // commit=80

  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true); // commit=120

  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), false);

  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), false);

  BOOST_CHECK (queue.pop (header, payload, consumer));
  BOOST_CHECK (payloadProducer == payload);

  headerProducer.seqNum = 123;
  BOOST_CHECK (queue.push (headerProducer, payloadProducer));
  BOOST_CHECK (payloadProducer == payload);

  BOOST_CHECK_EQUAL (queue.pop (header, payload, consumer), true);
  BOOST_CHECK_EQUAL (header.version,    headerProducer.version);
  BOOST_CHECK_EQUAL (header.type,       headerProducer.type);
  BOOST_CHECK_EQUAL (header.timestamp,  headerProducer.timestamp);
  BOOST_CHECK_EQUAL (header.seqNum,     3);
  BOOST_CHECK (payloadProducer == payload);

  BOOST_CHECK_EQUAL (queue.pop (header, payload, consumer), true);
  BOOST_CHECK_EQUAL (header.version,    headerProducer.version);
  BOOST_CHECK_EQUAL (header.type,       headerProducer.type);
  BOOST_CHECK_EQUAL (header.timestamp,  headerProducer.timestamp);
  BOOST_CHECK_EQUAL (header.seqNum,     123);
  BOOST_CHECK (payloadProducer == payload);
}

template <class Queue>
class Server : boost::noncopyable
{
public:
  Server (Queue &queue, size_t size, uint32_t rate)
  : m_data (size, 0),
    m_throttle (rate)
  {
    std::iota (std::begin (m_data), std::end (m_data), 1);

    m_thread = std::thread ([this, &queue]() {

      uint64_t seqNum = 0;

      Header   header;
      header.size = m_data.size ();

      m_ready = true;

      while (!m_stop)
      {
        header.seqNum    = ++seqNum;
        header.timestamp = nanoseconds_since_epoch (Clock::now ());

        while (!queue.push (header, m_data) && !m_stop)
        { }

        m_throttle.throttle ();
      }
    });
  }

  ~Server ()
  {
    m_stop = true;

    m_thread.join ();
  }

  bool ready () { return m_ready; }
  void stop ()  { m_stop = true; m_ready   = false; }
  void pause (bool pause) { m_pause   = pause; }
  void sendOne ()         { m_sendOne = true;  }

  const std::vector<uint8_t> &data () { return m_data; }

private:
  std::vector<uint8_t> m_data;

  std::atomic<bool> m_stop  = { false };
  std::atomic<bool> m_ready = { false };
  std::atomic<bool> m_pause = { true  };
  std::atomic<bool> m_sendOne = { false  };
  Throttle          m_throttle;
  std::thread       m_thread;

};

template <class Queue>
class Client : boost::noncopyable
{
public:
  Client (Queue &queue, size_t dataSize)
  : m_queue (queue)
  {
    m_data.resize (dataSize);

    m_thread = std::thread ([this] () {

      try
      {
        m_queue.register_consumer (m_consumer);

        auto &throughput =  m_stats.throughput ().summary ();
        auto &latency    =  m_stats.latency ().summary ();

        Header header;

        m_ready = true;

        while (!m_stop)
        {
          if (m_queue.pop (header, m_data, m_consumer))
          {
            throughput.next (sizeof (header) + m_data.size (), header.seqNum);

            latency.next ({ Clock::now () -
                  timepoint_from_nanoseconds_since_epoch (header.timestamp) });
          }
        }
       /*
        * Notifying the queue of a consumer exiting should occur from the thread
        * context of the consumer.
        *
        * Failure to do this will prevent the consumer slot from being reused.
        */
        m_queue.unregister_consumer (m_consumer);
      }
      catch (const std::exception &e)
      {
        m_stop = true;

        m_exceptions.push_back (std::string (e.what ()));
      }
    });
  }

  ~Client ()
  {
    m_stop = true;

    m_thread.join ();
  }

  bool ready () const { return m_ready; }

  void stop () { m_stop = true; m_ready = false;}

  const std::vector<uint8_t> &data () const { return m_data; }

  const PerformanceStats &stats ()    const { return m_stats;    }

  std::vector<std::string> &exceptions () { return m_exceptions; }

private:

  std::atomic<bool> m_ready = { false };

  Queue &m_queue;

  detail::ConsumerState m_consumer;

  std::vector<uint8_t> m_data;

  std::atomic<bool> m_stop = { false };

  std::thread m_thread;

  PerformanceStats m_stats;

  std::vector<std::string> m_exceptions;
};

using TestServer = Server<SPMCQueue<std::allocator<uint8_t>>>;
using TestClient = Client<SPMCQueue<std::allocator<uint8_t>>>;

BOOST_AUTO_TEST_CASE (ThreadedProducerSingleConsumer)
{
  ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (1024*1024*100);
#if CACHE_SIZE_ENABLED // re-enable prefetch functionality

  queue.resize_cache (1024);
#endif

  size_t   messageSize = 32+sizeof (Header);  // typical BBA message size
  uint32_t throughput  = 100e3;   // 100K message/sec

  TestServer server (queue, messageSize, throughput);

  while (!server.ready ())
  {
    std::this_thread::sleep_for (1ms);
  }

  TestClient client (queue, messageSize);

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  BOOST_CHECK_EQUAL (client.data ().size (), server.data ().size ());
  for (size_t i = 0; i < server.data ().size (); ++i)
  {
    BOOST_CHECK_EQUAL (client.data ()[i], server.data ()[i]);
  }

  // test throughput against a relatively conservative value
  auto &summary = client.stats ().throughput ().summary ();
  BOOST_CHECK (summary.messages () > 100);

  BOOST_TEST_MESSAGE ("throughput:\t" << summary.to_string ());
}


BOOST_AUTO_TEST_CASE (ThreadedProducerMultiConsumerNoMesssage)
{
  ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (1024*1024);

  size_t messageSize = 32;
  uint32_t throughput = 1e6;

  TestServer server (queue, messageSize, throughput);

  // allow the server to initialise
  while (!server.ready ())
  {
    std::this_thread::sleep_for (1ms);
  }

  const int clientCount = 2;
  std::vector<std::unique_ptr<TestClient>> clients;

  for (int i = 0; i < clientCount; ++i)
  {
    auto client = std::make_unique<TestClient> (queue, messageSize);

    while (!client->ready ())
    {
      std::this_thread::sleep_for (1ms);
    }

    clients.push_back (std::move (client));
  }

  BOOST_TEST_MESSAGE ("server throughput:\t" << throughput);
  BOOST_TEST_MESSAGE ("message size:\t\t"    << messageSize);

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  server.stop ();
  for (auto &client : clients)
  {
    client->stop ();
  }

  auto print = [] (const PerformanceStats& stats) {
    BOOST_TEST_MESSAGE (" throughput:\t\t"
      << stats.throughput ().summary ().to_string ());
  };

  for (auto &client : clients)
  {
    auto &stats = client->stats ();

    // test throughput against a relatively conservative value
    BOOST_CHECK ((stats.throughput ().summary ()
                       .messages_per_sec () / 1.0e6) > .8);

    BOOST_TEST_MESSAGE ("client");
    print (stats);
  }

  BOOST_TEST_MESSAGE("\nlatency stats for one of the clients");

  auto &latencies = clients.back ()->stats ().latency ().summary ();

  for (auto line : latencies.to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

template <class Queue>
class Client2 : boost::noncopyable
{
public:
  Client2 (Queue &queue, size_t dataSize)
  {
    m_thread = std::thread ([this, dataSize, &queue] () {

      try
      {
        SPMCSink<Queue> sink (queue);
        std::vector<uint8_t> data (dataSize);

        auto &throughput =  m_stats.throughput ().summary ();
        auto &latency    =  m_stats.latency ().summary ();

        Header header;

        while (!m_stop)
        {
          if (sink.next (header, data))
          {
            throughput.next (sizeof (header) + data.size (), header.seqNum);

            latency.next ({ Clock::now () -
                  timepoint_from_nanoseconds_since_epoch (header.timestamp) });
          }
        }
      }
      catch (const std::exception &e)
      {
        m_stop = true;

        m_exceptions.push_back (std::string (e.what ()));
      }
    });
  }

  ~Client2 ()
  {
    m_stop = true;

    m_thread.join ();
  }

  void stop () { m_stop = true; }

  const PerformanceStats &stats () const { return m_stats; }

  std::vector<std::string> &exceptions () { return m_exceptions; }

private:

  std::unique_ptr<SPMCSink<Queue>> m_sink;

  std::atomic<bool> m_stop = { false };

  std::thread m_thread;

  PerformanceStats m_stats;

  std::vector<std::string> m_exceptions;
};

BOOST_AUTO_TEST_CASE (TooManyConsumers)
{
  ScopedLogLevel log (error);

  /*
   * Set the maximum number of consumers to be two
   */
  const size_t maxClients = 2;

  using QueueType  = SPMCQueue<std::allocator<uint8_t>, maxClients>;
  using ClientType = Client<QueueType>;

  std::vector<std::unique_ptr<ClientType>> clients;

  QueueType queue (1024*1024*10);

  size_t messageSize  = 128;
  uint32_t throughput = 1e6;

  // Add the maximum number of clients
  clients.push_back (std::move (std::make_unique<ClientType>(queue, messageSize)));
  clients.push_back (std::move (std::make_unique<ClientType>(queue, messageSize)));

  Server<QueueType> server (queue, messageSize, throughput);

  BOOST_TEST_MESSAGE ("Expected server throughput:\t" << throughput);
  BOOST_TEST_MESSAGE ("Expected message size:\t\t"    << messageSize);

  // allow the clients to initialise
  std::this_thread::sleep_for (Milliseconds (100));

  BOOST_CHECK_EQUAL (clients.size (), 2);
  {
    BOOST_TEST_MESSAGE ("Add one client too many");
    /*
     * An exception should be thrown when registering more clients than the
     * queue is configured to accept.
     *
     * For the purposes of this test the exception is stored in an exception
     * pointer stored in the ClientType.
     */

    ClientType client (queue, messageSize);

    std::this_thread::sleep_for (Seconds (2));

    BOOST_CHECK_MESSAGE (client.exceptions ().size () == 1,
                         "An expected exception was not thrown");

    client.exceptions ().clear ();

    BOOST_CHECK_EQUAL (clients.size (), 2);

    client.stop ();
  }

  // stop one of the clients to create room for a new client to be added
  BOOST_TEST_MESSAGE ("Stop one of the clients, to free space for a new client");
  clients.resize (clients.size () -1);

  std::this_thread::sleep_for (Milliseconds (10));

  BOOST_TEST_MESSAGE ("Create and add a new client");
  ClientType client (queue, messageSize);

  std::this_thread::sleep_for (Milliseconds (10));

  BOOST_CHECK (client.exceptions ().empty ());

  std::this_thread::sleep_for (Milliseconds (10));

  BOOST_TEST_MESSAGE ("Stop all clients: " <<  clients.size ());

  for (auto &client : clients)
  {
    client->stop ();
  }

  BOOST_TEST_MESSAGE ("Stop server");

  server.stop ();
}

/*
 * Restart the client consuming data from a server
 */
BOOST_AUTO_TEST_CASE (RestartClient)
{
  ScopedLogLevel log (error);

  const size_t maxClients = 4;
  using QueueType = SPMCQueue<std::allocator<uint8_t>, maxClients>;
  QueueType queue (500);

  size_t   messageSize = 68; // packet size is 100 bytes including header
  uint32_t throughput = 1000;

  /*
   * Restart the server a few times. The client should detect the restart, reset
   * and consume the new data.
   */
  Server<QueueType> server (queue, messageSize, throughput);

  TimeDuration duration (Milliseconds (1500));

  for (int i = 0; i < 2; ++i)
  {
    double clientThroughput = 0;
    {
      Client<QueueType> client (queue, messageSize);

      /*
       * Allow the clients to initialise and receive some data.
       * Sleep for more than one second so that the throughput calc does not
       * round number of seconds down to zero.
       */
      std::this_thread::sleep_for (duration.nanoseconds ());

      clientThroughput = client.stats ().throughput ().summary ()
                               .messages_per_sec ();
    }

    BOOST_TEST_MESSAGE ("Client throughput msgs/sec="
                << throughput_messages_to_pretty (clientThroughput, duration)
                << " clientThroughput: " << clientThroughput
                << " target throughput: " << (throughput * 0.50));
    BOOST_CHECK (clientThroughput > (throughput * 0.50));
  }
}

BOOST_AUTO_TEST_CASE (RestartServer)
{
  ScopedLogLevel log (error);

  using QueueType = SPMCQueue<std::allocator<uint8_t>>;
  QueueType queue (500);

  size_t   messageSize = 68; // packet size is 100 bytes including header
  uint32_t throughput  = 1000;

  /*
   * Restart the server a few times. The client should detect the restart, reset
   * and consume the new data.
   */
  Client<QueueType> client (queue, messageSize);

  for (int i = 0; i < 4; ++i)
  {
    Server<QueueType> server (queue, messageSize, throughput);

    // allow the clients to initialise and receive some data
    std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

    auto clientThroughput = client.stats ().throughput ().summary ()
                               .messages_per_sec ();

    BOOST_TEST_MESSAGE ("Client throughput=" << clientThroughput);
    BOOST_CHECK (clientThroughput > (throughput * 0.80));

    server.stop ();
  }
}

BOOST_AUTO_TEST_CASE (SourceSinkInSharedMemory)
{
  using namespace boost;
  using namespace boost::interprocess;

  ScopedLogLevel log (error);

  std::string name = "SourceSinkInSharedMemory:Test";
  /*
   * RAII class to cleanup shared memory
   */
  struct RemoveSharedMemory
  {
    RemoveSharedMemory (const std::string & name) : name (name)
    { shared_memory_object::remove (name.c_str ()); }

    ~RemoveSharedMemory ()
    { shared_memory_object::remove (name.c_str ()); }

    std::string name;
  } cleanup (name);
  /*
   * Message and queue sizes in bytes
   */
  size_t capacity    = 20480;
  size_t messageSize = 32;
  /*
   * Create shared memory in which shared objects can be created
   */
  managed_shared_memory memory (open_or_create, name.c_str(),
                         capacity + SharedMemory::BOOK_KEEPING);

  SPMCSourceProcess source (name, name + ":queue", capacity);

  std::atomic<bool> stop = { false };

  auto producer = std::thread ([&stop, &source, &messageSize] () {

    std::vector<uint8_t> message (messageSize, 0);

    std::iota (std::begin (message), std::end (message), 1);

    while (!stop)
    {
      source.next (message);
    }
  });

  SPMCSinkProcess sink (name, name + ":queue");

  PerformanceStats stats;

  auto consumer = std::thread ([&stop, &sink, &stats, &messageSize] () {

    // consume messages from the shared memory queue
    std::vector<uint8_t> message  (messageSize, 0);
    std::vector<uint8_t> expected (messageSize, 0);
    std::iota (std::begin (expected), std::end (expected), 1);

    Header header;

    uint64_t count = 0;

    while (!stop)
    {
      if (sink.next (header, message))
      {
        if (count == 0)
        {
          count = header.seqNum;
        }
        else
        {
          ++count;
        }

        stats.update (header.size, header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));

        BOOST_CHECK (message == expected);

        message.clear ();
      }
      BOOST_CHECK_EQUAL (count, header.seqNum);
    }
  });

  std::this_thread::sleep_for (get_test_duration ().nanoseconds ());

  source.stop ();
  sink.stop ();

  stop = true;
  consumer.join ();
  producer.join ();

  BOOST_CHECK (stats.throughput ().summary ().messages_per_sec () > 1e6);
  BOOST_TEST_MESSAGE ("throughput "
                        << stats.throughput ().summary ().to_string ());

  auto &quantiles = stats.latency ().summary ().quantiles ();

  BOOST_CHECK (quantiles.empty () == false);

  auto median = static_cast<int64_t> (
                  boost::accumulators::p_square_quantile (quantiles.at (50)));

  BOOST_CHECK (Nanoseconds (median) < Milliseconds (2));

  BOOST_TEST_MESSAGE ("latencies (min/med/max): "
                        << stats.latency ().summary ().to_string ());
}

BOOST_AUTO_TEST_CASE (VariadicGetSize)
{
  uint8_t i = 3;

  BOOST_CHECK_EQUAL (get_size (i), sizeof (uint8_t));
  BOOST_CHECK_EQUAL (get_size (i), 1);

  Header header;

  BOOST_CHECK_EQUAL (get_size (header), sizeof (Header));

  std::vector<uint8_t> v1, v2;
  v1.resize (2);
  v2.resize (5);

  BOOST_CHECK_EQUAL (get_size (v1), v1.size ());

  BOOST_CHECK_EQUAL (get_size (header, v1), sizeof (Header)+ v1.size ());

  BOOST_CHECK_EQUAL (get_size (header, v1, v2),
                     sizeof (Header) + v1.size () + v2.size ());
}
