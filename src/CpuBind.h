#ifndef IPC_CPU_BIND_H
#define IPC_CPU_BIND_H

namespace spmc {

/*
 * Bind the current thread to a cpu
 */
void bind_to_cpu (int cpu);

} // namespace spmc

#endif // IPC_CPU_BIND_H