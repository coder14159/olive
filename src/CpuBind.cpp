#include "CpuBind.h"

#include <boost/log/trivial.hpp>

#include <pthread.h>

namespace spmc {

void bind_to_cpu (int cpu)
{
  if (cpu > -1)
  {
    cpu_set_t cpuset;

    CPU_ZERO (&cpuset);

    CPU_SET (cpu, &cpuset);

    auto thisThread = pthread_self ();

    auto result = pthread_setaffinity_np (thisThread,
                                    sizeof (cpu_set_t), &cpuset);
    if (result != 0)
    {
      BOOST_LOG_TRIVIAL(warning) << "Failed to bind to CPU #" << cpu
                               << " [" << strerror (result) << "]";
    }
    else
    {
      BOOST_LOG_TRIVIAL(info) << "Bound process to CPU #" << cpu;
    }
  }
}

} // namespace spmc
