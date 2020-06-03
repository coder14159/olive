#define SPMC_ENABLE_ASSERTS 1

#include "Assert.h"

#include "Buffer.h"
#include "LatencyStats.h"
#include "Logger.h"
#include "PerformanceStats.h"
#include "SPMCQueue.h"
#include "SPMCSink.h"
#include "SPMCStream.h"
#include "Throttle.h"
#include "Throughput.h"
#include "detail/SharedMemory.h"

#include <boost/core/noncopyable.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/scope_exit.hpp>

#include <exception>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE SPMCQueueTests
#include <boost/test/unit_test.hpp>

#define SPMC_DEBUG_ASSERT

using namespace spmc;

using namespace boost::log::trivial;

BOOST_AUTO_TEST_CASE (BasicBufferTests)
{
  spmc::ScopedLogLevel log (error);

  { // Add max data size
    Buffer<std::allocator<uint8_t>> buffer (5);
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

      BOOST_CHECK (out.size () == in.size ());
      BOOST_CHECK (out == in);
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

      BOOST_CHECK (out.size () == in.size ());
      BOOST_CHECK (out == in);
    }
  }
  { // resize
    Buffer<std::allocator<uint8_t>> buffer (5);
    std::vector<uint8_t> out;
    std::vector<uint8_t> in (5);
    std::iota (in.begin (), in.end (), 0);

    buffer.push (in);

    buffer.resize (10);
    BOOST_CHECK_EQUAL (buffer.capacity (), 10);
    BOOST_CHECK_EQUAL (buffer.size (), 0);
  }
}

BOOST_AUTO_TEST_CASE (BufferPopStruct)
{
  spmc::ScopedLogLevel log (error);

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

  BOOST_CHECK (in.a == out.a);
  BOOST_CHECK (in.b == out.b);
}

BOOST_AUTO_TEST_CASE (BufferConsumesFromSPSCQueue)
{
  spmc::ScopedLogLevel log (error);

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

BOOST_AUTO_TEST_CASE (SPMCQueueBasicTest)
{
  SPMCQueue<std::allocator<uint8_t>> queue (100);

  queue.allow_message_drops ();

  std::vector<uint8_t> payload;
  Header header;

  // test pushing enough data to wrap the buffer a few times
  for (int i = 1; i < 6; ++i)
  {
    std::vector<uint8_t> payload;
    {
      Header header;
      header.version = 1;
      header.type = 2;
      header.size = 5;
      header.seqNum = i;

      header.timestamp = nanoseconds_since_epoch (Clock::now ());

      std::vector<uint8_t> payload = { 0, 1, 2, 3, 4 };

      BOOST_CHECK_EQUAL (queue.push (header, payload), true);
    }

    {
      Header header;

      BOOST_CHECK_EQUAL (queue.pop (header, payload), true);
      BOOST_CHECK_EQUAL (header.version, 1);
      BOOST_CHECK_EQUAL (header.type,    2);
      BOOST_CHECK_EQUAL (header.seqNum,  i);

      BOOST_CHECK_EQUAL (payload.size (), 5);
      BOOST_CHECK_EQUAL (payload[0], 0);
      BOOST_CHECK_EQUAL (payload[1], 1);
      BOOST_CHECK_EQUAL (payload[2], 2);
      BOOST_CHECK_EQUAL (payload[3], 3);
      BOOST_CHECK_EQUAL (payload[4], 4);
    }
  }
}

BOOST_AUTO_TEST_CASE (SPMCQueuePushPod)
{
  SPMCQueue<std::allocator<uint8_t>> queue (100);

  struct Payload
  {
    int  i;
    char c;
  };

  Payload payloadIn = { 100, 'z' };

  auto time = nanoseconds_since_epoch (Time::now ());

  Header  headerIn  = { 1, 2, sizeof (Payload), 3, time };

  BOOST_CHECK (queue.push (headerIn, payloadIn));

  std::vector<uint8_t> data;

  Header headerOut;

  BOOST_CHECK (queue.pop (headerOut, data));
  BOOST_CHECK (headerIn.version == headerOut.version);

  Payload *payloadOut = reinterpret_cast<Payload*> (data.data ());

  BOOST_CHECK (payloadIn.i == payloadOut->i);
  BOOST_CHECK (payloadIn.c == payloadOut->c);

}


BOOST_AUTO_TEST_CASE (SlowConsumer)
{
  spmc::ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (100);

  queue.allow_message_drops ();

  Header headerProducer;
  headerProducer.version = 1;
  headerProducer.type = 2;
  headerProducer.size = 5;
  headerProducer.seqNum = 1;
  headerProducer.timestamp = nanoseconds_since_epoch (Time::now ());

  std::vector<uint8_t> payloadProducer = { 0, 1, 2, 3, 4 };

  queue.push (headerProducer, payloadProducer);
  Header header;
  std::vector<uint8_t> payload;

  BOOST_CHECK_EQUAL (queue.pop (header, payload), true);

  BOOST_CHECK_EQUAL (header.version,    headerProducer.version);
  BOOST_CHECK_EQUAL (header.type,       headerProducer.type);
  BOOST_CHECK_EQUAL (header.timestamp,  headerProducer.timestamp);

  BOOST_CHECK_EQUAL (payload.size (), 5);
  for (size_t i = 0; i < payload.size (); ++i)
  {
    BOOST_CHECK_EQUAL (payload[i], payloadProducer[i]);
  }

  // send enough data to wrap the buffer and cause the consumer to drop
  queue.push (headerProducer, payloadProducer);
  queue.push (headerProducer, payloadProducer);
  queue.push (headerProducer, payloadProducer);
  queue.push (headerProducer, payloadProducer);

  BOOST_CHECK_EQUAL (queue.pop (header, payload), false);

  headerProducer.seqNum = 123;
  queue.push (headerProducer, payloadProducer);

  BOOST_CHECK_EQUAL (queue.pop (header, payload), true);
  BOOST_CHECK_EQUAL (header.seqNum, 123);
}

BOOST_AUTO_TEST_CASE (SlowConsumerPrefetch)
{
  spmc::ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (128);

  queue.cache_size (85);

  size_t payloadSize = 8;

  Header headerIn;
  headerIn.version = 1;
  headerIn.type = 2;
  headerIn.size = payloadSize;
  headerIn.seqNum = 1;
  headerIn.timestamp = nanoseconds_since_epoch (Time::now ());

  std::vector<uint8_t> payloadIn (payloadSize);
  std::iota (std::begin (payloadIn), std::end (payloadIn), 1);

  BOOST_CHECK (queue.push (headerIn, payloadIn)); ++headerIn.seqNum;
  BOOST_CHECK (queue.push (headerIn, payloadIn)); ++headerIn.seqNum;

  Header headerOut;
  std::vector<uint8_t> payloadOut;
  BOOST_CHECK (queue.pop (headerOut, payloadOut));
  BOOST_CHECK_EQUAL (headerOut.seqNum, 1);
  BOOST_CHECK (payloadIn == payloadIn);

  bool ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);
  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);

  BOOST_CHECK (queue.pop (headerOut, payloadOut));
  BOOST_CHECK_EQUAL (headerOut.seqNum, 2);
  BOOST_CHECK (payloadIn == payloadIn);

  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);
  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (!ret);
  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (!ret);

  ret = queue.pop (headerOut, payloadOut);
  BOOST_CHECK (ret);
  BOOST_CHECK_EQUAL (headerOut.seqNum, 3);
  BOOST_CHECK (payloadIn == payloadIn);
}

BOOST_AUTO_TEST_CASE (ConsumerPrefetchSmallerThanMessageSize)
{
  spmc::ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (1024);

  queue.cache_size (50);

  size_t payloadSize = 128;

  Header headerIn;
  headerIn.version = 1;
  headerIn.type = 2;
  headerIn.size = payloadSize;
  headerIn.seqNum = 1;
  headerIn.timestamp = nanoseconds_since_epoch (Time::now ());

  std::vector<uint8_t> payloadIn (payloadSize);
  std::iota (std::begin (payloadIn), std::end (payloadIn), 1);

  BOOST_CHECK (queue.push (headerIn, payloadIn)); ++headerIn.seqNum;
  BOOST_CHECK (queue.push (headerIn, payloadIn)); ++headerIn.seqNum;

  Header headerOut;
  std::vector<uint8_t> payloadOut;
  BOOST_CHECK (queue.pop (headerOut, payloadOut));
  BOOST_CHECK_EQUAL (headerOut.seqNum, 1);
  BOOST_CHECK (payloadIn == payloadIn);

  bool ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);
  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);

  BOOST_CHECK (queue.pop (headerOut, payloadOut));
  BOOST_CHECK_EQUAL (headerOut.seqNum, 2);
  BOOST_CHECK (payloadIn == payloadIn);

  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);
  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);
  ret = queue.push (headerIn, payloadIn); ++headerIn.seqNum;
  BOOST_CHECK (ret);

  ret = queue.pop (headerOut, payloadOut);
  BOOST_CHECK (ret);
  BOOST_CHECK_EQUAL (headerOut.seqNum, 3);
  BOOST_CHECK (payloadIn == payloadIn);
}

BOOST_AUTO_TEST_CASE (SlowConsumerNoMessageDrops)
{
  SPMCQueue<std::allocator<uint8_t>> queue (128);

  Header headerProducer;
  headerProducer.version = 1;
  headerProducer.type = 2;
  headerProducer.size = 5;
  headerProducer.seqNum = 1;
  headerProducer.timestamp = nanoseconds_since_epoch (Time::now ());

  std::vector<uint8_t> payloadProducer = { 1, 2, 3, 4, 5 };

  Header header;
  std::vector<uint8_t> payload;
  /*
   * The first call to pop initialises the consumer and ensures it is ready to
   * start consuming from the next message to arrive
   */
  BOOST_CHECK_EQUAL (queue.pop (header, payload), false);

  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true);

  BOOST_CHECK_EQUAL (queue.pop (header, payload), true);

  BOOST_CHECK_EQUAL (header.version,    headerProducer.version);
  BOOST_CHECK_EQUAL (header.type,       headerProducer.type);
  BOOST_CHECK_EQUAL (header.timestamp,  headerProducer.timestamp);

  BOOST_CHECK_EQUAL (payload.size (), 5);
  for (size_t i = 0; i < payload.size (); ++i)
  {
    BOOST_CHECK_EQUAL (payload[i], payloadProducer[i]);
  }

  // send enough data to wrap the buffer and exert back pressure on the producer
  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true);

  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true);

  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true);

  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), false);

  ++headerProducer.seqNum;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), false);

  BOOST_CHECK_EQUAL (queue.pop (header, payload), true);
  BOOST_CHECK (payloadProducer == payload);

  headerProducer.seqNum = 123;
  BOOST_CHECK_EQUAL (queue.push (headerProducer, payloadProducer), true);
  BOOST_CHECK (payloadProducer == payload);

  // no message drops enabled so pops the next unconsumed message
  BOOST_CHECK_EQUAL (queue.pop (header, payload), true);
  BOOST_CHECK_EQUAL (header.version,    headerProducer.version);
  BOOST_CHECK_EQUAL (header.type,       headerProducer.type);
  BOOST_CHECK_EQUAL (header.timestamp,  headerProducer.timestamp);
  for (size_t i = 0; i < payload.size (); ++i)
  {
    BOOST_CHECK_EQUAL (payload[i], payloadProducer[i]);
  }
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

      while (!m_stop)
      {
        header.seqNum    = ++seqNum;
        header.timestamp = nanoseconds_since_epoch (Time::now ());

        while (!queue.push (header, m_data))
        { }

        m_throttle.throttle ();
      }
    });
  }

  ~Server ()
  {
    stop ();
    m_thread.join ();
  }

  void stop ()            { m_stop    = true;  }
  void pause (bool pause) { m_pause   = pause; }
  void sendOne ()         { m_sendOne = true;  }

  const std::vector<uint8_t> &data () { return m_data; }

private:
  std::vector<uint8_t> m_data;

  std::atomic<bool> m_stop  = { false };
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
  : m_queue (queue),
    m_data (dataSize, 0),
    m_latencyStats (100)
  {
    m_latencyStats.interval ().enable (true);
    m_latencyStats.summary ().enable (true);

    m_throughputStats.interval ().enable (true);
    m_throughputStats.summary ().enable (true);

    m_thread = std::thread ([this] () {

      try
      {
        Header header;

        if (m_messageDropsAllowed)
        {
          // call no drops in the client thread context
          m_queue.allow_message_drops ();
        }

        while (!m_stop)
        {
          if (m_queue.pop (header, m_data))
          {
            m_throughputStats.next (sizeof (header) + m_data.size (), header.seqNum);

            m_latencyStats.next (TimePoint (Nanoseconds (header.timestamp)));
          }
        }
       /*
        * Notifying the queue of a consumer exiting should occur from the thread
        * context of the consumer.
        *
        * Failure to do this will prevent the consumer slot from being reused.
        */
        m_queue.unregister_consumer ();
      }
      catch (const std::exception &e)
      {
        std::cerr << "Client exception caught: " << e.what () << std::endl;

        m_queue.unregister_consumer ();
      }
    });
  }

  ~Client ()
  {
    m_stop = true;

    m_thread.join ();
  }

  void allow_message_drops () { m_messageDropsAllowed = true; }

  void stop () { m_stop = true; }

  const std::vector<uint8_t> &data () const { return m_data; }

  const LatencyStats    &latencyStats ()    const { return m_latencyStats;    }
  const ThroughputStats &throughputStats () const { return m_throughputStats; }

private:
  Queue &m_queue;

  std::vector<uint8_t> m_data;

  bool m_messageDropsAllowed = false;

  std::atomic<bool> m_stop  = { false };

  std::thread m_thread;

  ThroughputStats m_throughputStats;
  LatencyStats    m_latencyStats;

};

using DefaultServer = Server<SPMCQueue<std::allocator<uint8_t>>>;
using DefaultClient = Client<SPMCQueue<std::allocator<uint8_t>>>;

BOOST_AUTO_TEST_CASE (ThreadedProducerSingleConsumer)
{
  spmc::ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (1024*1024*100);
  queue.cache_size (1024);

  size_t   messageSize = 96+sizeof (Header);  // typical BBA message size
  uint32_t throughput  = 8e6;   // 4 M message/sec

  DefaultClient client (queue, messageSize);
  DefaultServer server (queue, messageSize, throughput);

  std::this_thread::sleep_for (Seconds (5));

  auto now = Time::now ();

  BOOST_CHECK_EQUAL (client.data ().size (), server.data ().size ());
  for (size_t i = 0; i < server.data ().size (); ++i)
  {
    BOOST_CHECK_EQUAL (client.data ()[i], server.data ()[i]);
  }

  // test throughput against a relatively conservative value
  auto &summary = client.throughputStats ().summary ();
  BOOST_CHECK (summary.messages () > 1e6);
  BOOST_CHECK (summary.dropped () == 0);

  BOOST_TEST_MESSAGE ("messages dropped:\t" << summary.dropped ());

  BOOST_TEST_MESSAGE("throughput:\t" << std::fixed << std::setprecision (2)
      << summary.messages_per_sec (now) / 1.0e6
      << "\tM msg/sec");

  BOOST_TEST_MESSAGE("throughput:\t" << std::fixed << std::setprecision (0)
      << client.throughputStats ()
               .summary ().megabytes_per_sec (now) << "\tMB/sec");
}

BOOST_AUTO_TEST_CASE (ThreadedProducerMultiConsumerAllowDrops)
{
  spmc::ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (1024*1024*10);

  size_t messageSize = 128;
  std::vector<std::unique_ptr<DefaultClient>> clients;

  for (int i = 0; i < 2; ++i)
  {
    clients.push_back (std::make_unique<DefaultClient> (queue, 0));
  }

  // allow the clients to initialise
  std::this_thread::sleep_for (Milliseconds (10));

  uint32_t throughput = 1e6;
  DefaultServer server (queue, messageSize, throughput);

  BOOST_TEST_MESSAGE ("server throughput:\t" << throughput);
  BOOST_TEST_MESSAGE ("message size:\t\t"    << messageSize);

  std::this_thread::sleep_for (Seconds (3));

  auto now = Time::now ();

  auto print = [&now] (const Throughput& stats) {
    BOOST_TEST_MESSAGE ("  dropped:\t\t" << stats.dropped ());
    BOOST_TEST_MESSAGE ("  M msgs/s:\t\t"
                   << std::fixed << std::setprecision (2)
                   << stats.messages_per_sec (now) / 1.0e6);
    BOOST_TEST_MESSAGE ("  MB/s throughput:\t"
                        << std::fixed << std::setprecision (0)
                        << stats.megabytes_per_sec (now));
  };

  server.stop ();

  for (auto &client : clients)
  {
    client->stop ();

    // test throughput against a relatively conservative value
    BOOST_CHECK (
      (client->throughputStats ().summary ().messages_per_sec (now) / 1.0e6) > .99);
    BOOST_TEST_MESSAGE ("client");
    print (client->throughputStats ().summary ());
  }

  BOOST_TEST_MESSAGE("\nlatency stats for one of the clients");
  for (auto line : clients[0]->latencyStats ().interval ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (ThreadedProducerMultiConsumerNoMesssageDropsAllowed)
{
  spmc::ScopedLogLevel log (error);

  SPMCQueue<std::allocator<uint8_t>> queue (1024*1024);

  int clientCount = 2;
  size_t messageSize = 128;
  std::vector<std::unique_ptr<DefaultClient>> clients;

  for (int i = 0; i < clientCount; ++i)
  {
    auto client = std::make_unique<DefaultClient>(queue, 0);
    clients.push_back(std::move (client));
  }

  // allow the clients to initialise
  std::this_thread::sleep_for (Milliseconds (10));

  uint32_t throughput = 1e6;
  DefaultServer server (queue, messageSize, throughput);

  BOOST_TEST_MESSAGE ("server throughput:\t" << throughput);
  BOOST_TEST_MESSAGE ("message size:\t\t"    << messageSize);

  std::this_thread::sleep_for (Seconds (5));

  auto now  = Time::now ();

  auto print = [&now] (const Throughput& stats) {
    BOOST_TEST_MESSAGE (" dropped:\t\t" << stats.dropped ());
    BOOST_TEST_MESSAGE (" M msgs/s:\t\t"
                   << std::fixed << std::setprecision (2)
                   << stats.messages_per_sec (now) / 1.0e6);
    BOOST_TEST_MESSAGE (" MB/s throughput:\t"
                        << std::fixed << std::setprecision (0)
                        << stats.megabytes_per_sec (now));
  };

  server.stop ();

  for (auto &client : clients)
  {
    client->stop ();

    // test throughput against a relatively conservative value
    BOOST_CHECK ((client->throughputStats ().summary ()
                            .messages_per_sec (now) / 1.0e6) > .99);
    BOOST_TEST_MESSAGE ("client");

    print (client->throughputStats ().summary ());
  }

  BOOST_TEST_MESSAGE("\nlatency stats for one of the clients");
  for (auto line : clients[0]->latencyStats ().summary ().to_strings ())
  {
    BOOST_TEST_MESSAGE (line);
  }
}

BOOST_AUTO_TEST_CASE (TooManyConsumers)
{
  spmc::ScopedLogLevel log (error);

  /*
   * Set the maximum number of consumers to be three
   */
  const size_t maxClients = 2;

  using QueueType = SPMCQueue<std::allocator<uint8_t>, maxClients>;
  QueueType queue (1024*1024*10);

  using ClientType = Client<QueueType>;

  std::vector<std::unique_ptr<ClientType>> clients;

  size_t messageSize  = 128;
  uint32_t throughput = 1e6;

  clients.push_back (std::move (std::make_unique<ClientType>(queue, messageSize)));
  clients.push_back (std::move (std::make_unique<ClientType>(queue, messageSize)));

  Server<QueueType> server (queue, messageSize, throughput);

  BOOST_TEST_MESSAGE ("server throughput:\t" << throughput);
  BOOST_TEST_MESSAGE ("message size:\t\t"    << messageSize);

  // allow the clients to initialise
  std::this_thread::sleep_for (Milliseconds (5));

  int exceptions_caught = 0;
  try
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

    std::this_thread::sleep_for (Seconds (10));

    BOOST_TEST_MESSAGE ("'Too many clients' exception should have been thrown");
  }
  catch (const std::exception &e)
  {
    ++exceptions_caught;
  }

  BOOST_CHECK_MESSAGE (exceptions_caught == 1,
      "The expected 'Too many clients' exception was not thrown");

  // stop one of the clients to create room for a new client to be added
  clients.resize (clients.size () -1);

  std::this_thread::sleep_for (Milliseconds (10));

  try
  {
    BOOST_TEST_MESSAGE ("Add a client");

    ClientType client (queue, messageSize);

    std::this_thread::sleep_for (Milliseconds (10));
  }
  catch (const std::exception &e)
  {
    BOOST_CHECK_MESSAGE (false,
        "An unexpected exception was caught: " << e.what ());
  }

  std::this_thread::sleep_for (Milliseconds (10));


  BOOST_TEST_MESSAGE ("Stop all clients");
  for (auto &client : clients)
  {
    client->stop ();
  }

  server.stop ();
}

/*
 * Restart the client consuming from the same server
 */
BOOST_AUTO_TEST_CASE (RestartClient)
{
  using QueueType = SPMCQueue<std::allocator<uint8_t>>;
  QueueType queue (500);

  size_t   messageSize = 68; // sizeof (header) + messageSize = 100 bytes
  uint32_t throughput = 1000;

  /*
   * Restart the server a few times. The client should detect the restart, reset
   * and consume the new data.
   */

  Server<QueueType> server (queue, messageSize, throughput);

  for (int i = 0; i < 5; ++i)
  {
    Client<QueueType> client (queue, messageSize);

    // allow the clients to initialise and receive some data
    std::this_thread::sleep_for (Seconds (1));

    auto clientThroughput = client.throughputStats ()
                                  .summary ()
                                  .messages_per_sec (Time::now ());

    BOOST_TEST_MESSAGE ("Client throughput=" << clientThroughput);
    BOOST_CHECK (clientThroughput > (throughput * 0.80));
  }
}


/*
 * Server restart without client restart needs further work
 */
BOOST_AUTO_TEST_CASE (RestartServer)
{
  spmc::ScopedLogLevel log (error);

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
    std::this_thread::sleep_for (Seconds (2));

    auto clientThroughput = client.throughputStats ()
                                  .summary ()
                                  .messages_per_sec (Time::now ());

    BOOST_TEST_MESSAGE ("Client throughput=" << clientThroughput);
    BOOST_CHECK (clientThroughput > (throughput * 0.80));

    server.stop ();
  }
}

#if FIXME
BOOST_AUTO_TEST_CASE (SinkStreamInSharedMemory)
{
  using namespace boost;
  using namespace boost::interprocess;

  spmc::ScopedLogLevel log (error);

  std::string name = "SinkStreamInSharedMemory:Test";

  struct RemoveSharedMemory
  {
    RemoveSharedMemory (const std::string & name) : name (name)
    { shared_memory_object::remove (name.c_str ()); }

    ~RemoveSharedMemory ()
    { shared_memory_object::remove (name.c_str ()); }

    std::string name;
  } cleanup (name);

  PerformanceStats stats;
  stats.throughput ().summary ().enable (true);
  stats.latency ().summary ().enable (true);

  /*
   * Message and queue sizes in bytes
   */
  size_t capacity    = 204800;
  size_t messageSize = 128;

  /*
   * Create shared memory in which shared objects can be created
   */
  managed_shared_memory (open_or_create, name.c_str(),
                         capacity + SharedMemory::BOOK_KEEPING);

  SPMCSinkProcess sink (name, name + ":queue", capacity);

  std::atomic<bool> stop = { false };

  auto producer = std::thread ([&stop, &sink, &messageSize] () {

    std::vector<uint8_t> message (messageSize, 0);

    std::iota (std::begin (message), std::end (message), 1);

    while (!stop)
    {
      sink.next (message);
    }
  });

  // consume messages from the shared memory queue
  std::vector<uint8_t> message  (messageSize, 0);
  std::vector<uint8_t> expected (messageSize, 0);
  std::iota (std::begin (expected), std::end (expected), 1);

  bool allowMessageDrops = false;
  size_t prefetchSize    = 0;
  SPMCStreamProcess stream (name, name + ":queue",
                            allowMessageDrops, prefetchSize);

  Header header;

  uint64_t start = 0;
  uint64_t count = 0;
  count = 0;

  while (true)
  {
    if (stream.next (header, message))
    {
      if (count == 0)
      {
        count = start = header.seqNum;
      }
      else
      {
        ++count;

        stats.update (header.size, header.seqNum,
                      TimePoint (Nanoseconds (header.timestamp)));

        BOOST_ASSERT (count == header.seqNum); // no message drops

        BOOST_CHECK (message == expected);
      }

      message.clear ();
    }

    if ((count - start) == 1000000)
    {
      sink.stop ();
      stop = true;
      break;
    }
  }

  producer.join ();
}
#endif