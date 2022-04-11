#ifndef OLIVE_CPU_BIND_H
#define OLIVE_CPU_BIND_H

namespace olive {

/*
 * Bind the current thread to a cpu
 */
void bind_to_cpu (int cpu);

} // namespace olive

#endif // OLIVE_CPU_BIND_H