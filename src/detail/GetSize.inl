#include <iostream>
/*
 * Add additional get_size () functions, similar to the vector specialisation,
 * to support other types.
 */
namespace spmc {

namespace {

/*
 * Additional spacializations for types copying to/from the queue.
 * The type must have data which is contiguous in memory
 */
template<typename T>
size_t get_size (const std::vector<T> &v)
{
  return v.size ();
}

template<typename T>
size_t get_size (const T &pod)
{
  /*
   * If this assert fires a get_size function needs to be created for the type
   */
  static_assert (std::is_trivially_copyable<T>::value,
      "A new get_size () function must be created for a non-trivial type");

  return sizeof (pod);
}

} // namespace {

template<typename Head, typename...Tail>
size_t get_size (const Head& head, const Tail&...tail)
{
  return get_size (head) + get_size (tail...);
}

} // namespace spmc