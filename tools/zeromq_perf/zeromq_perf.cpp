#include "Logger.h"
#include "PerformanceStats.h"
#include "Throttle.h"

#include "spmc_argparse.h"
#include "spmc_filesystem.h"
#include "spmc_time.h"
#include "spmc_time_duration.h"
#include "spmc_timer.h"
#include "spmc_signal_catcher.h"

#include <zmq.hpp>

#include <atomic>
#include <iostream>
#include <iomanip>
#include <memory>

using namespace spmc;
using namespace spmc;

struct __attribute__ ((packed)) Frame
{
  char     topic     = 'A';
  uint8_t  version   = 1;
  uint8_t  type      = 0;
  size_t   size      = 0;
  uint64_t seqNum    = 0;
  int64_t  timestamp = spmc::TimeDuration::minimum ().nanoseconds ();
  uint8_t  payload[0];
};

std::atomic<bool> g_stop = { false };

const std::string tcp_server_address = "tcp://*:55555";
const std::string tcp_client_address = "tcp://localhost:55555";
const std::string ipc_path           = "/tmp/zmq-feed";

void server (zmq::context_t &context, uint32_t rate, int highWaterMark,
             size_t size, bool tcp)
{
  ipc::logger ().notice () << "start zeromq server";
  ipc::logger ().info () << "message-size\t= " << size;
  ipc::logger ().info () << "iohwm\t\t= "      << highWaterMark;
  ipc::logger ().info () << "method\t\t= " << (tcp ? "tcp" : "domain sockets");
  ipc::logger ().info () << "rate\t\t= "
                         << ((rate == 0) ? "max" : std::to_string (rate));

  std::string address = (tcp ? tcp_server_address : "ipc://" + ipc_path + "/0");

  zmq::socket_t socket (context, ZMQ_PUB);

  socket.setsockopt (ZMQ_SNDHWM, &highWaterMark, sizeof (highWaterMark));

  socket.bind (address);

  Throttle throttle (rate);

  Timer timer;

  int64_t counter = 0;

  try
  {
    std::vector<uint8_t> data (size);

    std::iota (std::begin (data), std::end (data), 1);

    while (true)
    {
      throttle.throttle();

      zmq::message_t message (sizeof (Frame) + size);

      auto frame = reinterpret_cast<Frame *> (message.data ());

      frame->topic     = 'A';
      frame->version   = 1;
      frame->size      = size;
      frame->timestamp = Time::now ().nanoseconds ();
      frame->seqNum    = ++counter;

      memcpy (&(frame->payload[0]), data.data (), data.size ());

      /*
       * Non-blocking send as alternative for closing the context in the signal
       * handler thread (which causes race codition problems)
       */
      if (!socket.send (message, ZMQ_NOBLOCK))
      { }
    }
  }
  catch (const zmq::error_t &e)
  {
    logger ().notice () << e.what ();
  }

  timer.stop ();

  double throughput = (counter / 1.0e6) / timer.elapsed ().seconds ();

  logger ().info () << "server: "     << throughput << " M msgs/s "
                      << "total sent: " << counter;
}

void client (zmq::context_t &context, int highWaterMark, bool test, bool tcp,
             const std::string &directory,
             bool intervalStats, bool throughputStats, bool latencyStats)
{
  logger ().notice () << "start zeromq client";

  auto socketType     = ZMQ_SUB;
  std::string address = (tcp ? tcp_client_address : "ipc://" + ipc_path + "/0");

  try
  {

    zmq::socket_t socket (context, ZMQ_SUB);

    char topic = 'A';
    socket.setsockopt (ZMQ_SUBSCRIBE, &topic, sizeof (topic));
    socket.setsockopt (ZMQ_RCVHWM, &highWaterMark, sizeof (highWaterMark));

    socket.connect (address);

    std::vector<uint8_t> expected;
    zmq::message_t message;

    PerformanceStats stats (directory);

    stats.throughput ().summary () .enable (throughputStats);
    stats.throughput ().interval ().enable (throughputStats && intervalStats);

    stats.latency ().summary () .enable (latencyStats);
    stats.latency ().interval ().enable (latencyStats && intervalStats);

    while (true)
    {
      if (socket.recv (&message, ZMQ_NOBLOCK) > 0)
      {
        auto *frame = static_cast<const Frame*> (message.data ());

        // reset seqNum on startup and if the server was restarted
        if (frame->seqNum == 0 || frame->seqNum == 1)
        {
          // reset stats?
        }

        stats.update (sizeof (Frame), frame->size,
                      frame->seqNum,
                      Time::deserialise (frame->timestamp));

        if (test)
        {
          const uint8_t *payload = &(frame->payload[0]);

          if (expected.size () < frame->size)
          {
            expected.resize (frame->size);
            std::iota (std::begin (expected), std::end (expected), 1);
          }

          for (uint8_t i = 0; i < expected.size (); ++i)
          {
            assert (expected[i] == frame->payload[i]);
          }
        }
      }
    }
  }
  catch (const zmq::error_t &e)
  {
  }
}


int main (int argc, char *argv[])
{
  // Parse command line options
  std::string directory;
  std::string level ("NOTICE");

  int rate              = 0;
  int highWaterMark     = 10000000;
  int ioThreads         = 2;
  size_t messageSize    = 1024;
  bool isClient         = false;
  bool tcp              = false;
  bool test             = false;
  bool intervalStats    = false;
  bool throughputStats  = false;
  bool latencyStats     = false;


  ArgParse ().optional ("--client",
                        "consumes data", isClient)
             .optional ("--iohwm <high-water-mark>",
                        "socket high water mark", highWaterMark)
             .optional ("--iothreads <num-threads>",
                        "number of I/O threads per context", ioThreads)
             .optional ("--rate <msgs/sec>",
                        "send data rate message /second "
                        "zero indicates no throttling", rate)
             .optional ("--message-size <message-size>",
                        "size of each message sent", messageSize)
             .optional ("--test",
                        "validate arriving messages", test)
             .optional ("--tcp",
                        "set to tcp (default is unix domain sockets)", tcp)
             .optional ("--directory <path>",
                        "directory for statistics files", directory)
             .optional ("--interval-stats", "periodically print statistics",
                        intervalStats)
             .optional ("--latency-stats", "enable latency statistics",
                        latencyStats)
             .optional ("--throughput-stats", "enable throughput statistics",
                        throughputStats)
             .optional ("--loglevel <level>", "logging level", level)
             .choices  (Logger::level_strings ())
             .description ("zeromq client or server for performance testing")
             .run (argc, argv);

  zmq::context_t context (ioThreads);

  SignalCatcher signalCatcher ({SIGINT, SIGTERM}, [&isClient, &context] (int) {

      logger ().notice () << "stop " << (isClient ? "client" : "server");

      /*
       * Closing the C++ context unblocks and terminates the socket from a
       * separate thread.
       */
      context.close ();
  });

  if (!tcp)
  {
    CHECK_API (make_path (ipc_path));
  }

  if (isClient)
  {
    if (rate)
    {
      std::cout << "The rate option is invalid for a client" << std::endl;
      return 1;
    }

    client (context, highWaterMark, test, tcp, directory,
            intervalStats, throughputStats, latencyStats);
  }
  else
  {
    server (context, rate, highWaterMark, messageSize, tcp);
  }

  return 1;
}
