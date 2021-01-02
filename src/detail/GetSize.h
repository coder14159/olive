#ifndef IPC_DETAIL_SPMC_GET_SIZE_H
#define IPC_DETAIL_SPMC_GET_SIZE_H

#include <vector>
namespace spmc {

/*
 * Function get_size () returns the total size of a list of parameters.
 *
 * Currently supported are POD types and vector
 */
template<typename Head, typename...Tail>
size_t get_size (const Head& head, const Tail&...tail);

} // namespace spmc

#include "GetSize.inl"

#endif // IPC_DETAIL_SPMC_GET_SIZE_H