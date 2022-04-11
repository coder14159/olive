#ifndef OLIVE_DETAIL_SPMC_GET_SIZE_H
#define OLIVE_DETAIL_SPMC_GET_SIZE_H

#include <vector>

namespace olive {

/*
 * Function get_size () returns the total size of a list of parameters.
 *
 * Currently supported types are POD types and std::vector
 */
template<typename Head, typename...Tail>
size_t get_size (const Head& head, const Tail&...tail);

} // namespace olive

#include "GetSize.inl"

#endif // OLIVE_DETAIL_SPMC_GET_SIZE_H