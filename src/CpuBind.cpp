#include <boost/log/trivial.hpp>

#include <pthread.h>

namespace spmc {

void bind_to_cpu (int cpu)
{
  if (cpu != -1)
  {
    cpu_set_t cpuset;

    CPU_ZERO (&cpuset);

    CPU_SET (cpu, &cpuset);

    auto thisThread = pthread_self ();

    pthread_setaffinity_np (thisThread, sizeof (cpu_set_t), &cpuset);

    BOOST_LOG_TRIVIAL(info) << "bind to cpu: " << cpu;
  }
}

} // namespace spmc
