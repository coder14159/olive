#ifndef IPC_THROUGHPUT_STATS_H
#define IPC_THROUGHPUT_STATS_H

#include "Throughput.h"

namespace spmc {

/*
 * Helper class for throughput interval and summary logging
 */
class ThroughputStats
{
public:
  /*
   * Compute throughput statistics
   */
  ThroughputStats ();

  /*
   * Compute throughput statistics and store them to disk when write ()
   * method is called
   */
  ThroughputStats (const std::string &directory);

  ~ThroughputStats ();

  /*
   * Write throughput data to file if enabled
   */
  void write ();

  void next (uint64_t bytes, uint64_t seqNum);

  void next (uint64_t header, uint64_t payload, uint64_t seqNum);

  Throughput&       interval ()       { return m_interval;  }
  const Throughput& interval () const { return m_interval;  }

  Throughput&       summary ()        { return m_summary;   }
  const Throughput& summary ()  const { return m_summary;   }

  void reset_interval ();

private:
  Throughput m_interval;
  Throughput m_summary;
};

}

#endif // IPC_THROUGHPUT_STATS_H
