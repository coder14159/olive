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

  bool is_stopped () const;
  bool is_running () const;

  Throughput&       interval ()       { return m_interval;  }
  const Throughput& interval () const { return m_interval;  }

  Throughput&       summary ()        { return m_summary;   }
  const Throughput& summary ()  const { return m_summary;   }

private:
  Throughput m_interval;
  Throughput m_summary;
};

}

#endif // IPC_THROUGHPUT_STATS_H
