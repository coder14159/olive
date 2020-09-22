#include "detail/CXXOptsHelper.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/log/trivial.hpp>

#include <exception>
#include <iostream>
#include <string>

using namespace spmc;

namespace bi = boost::interprocess;

namespace {

CxxOptsHelper parse (int argc, char* argv[])
{
  cxxopts::Options cxxopts ("remove_shared_memory",
                           "Delete instances of named shared memory");

  std::vector<std::string> names;

  cxxopts.add_options ()
    ("h,help", "Remove named shared memory instance from the local machine")
    ("names", "Comma separated list of shared memory names to remove",
      cxxopts::value<std::vector<std::string>> (names));

  spmc::CxxOptsHelper options (cxxopts.parse (argc, argv));

  options.positional ("names", names);

  if (options.exists ("help"))
  {
    std::cout << cxxopts.help ({"", "Group"}) << std::endl;

    exit (EXIT_SUCCESS);
  }

  return options;
}

} // namespace {

int main(int argc, char* argv[]) try
{
  for (auto name : parse (argc, argv).values ("names"))
  {
    std::cout << "Shared memory '" << name << "' " ;
    if (bi::shared_memory_object::remove (name.c_str ()))
    {
      std::cout << "removed" << std::endl;
    }
    else
    {
      std::cout << "not removed" << std::endl;
    }
  }

  return EXIT_SUCCESS;
}
catch (const std::exception &e)
{
  std::cerr << e.what () << std::endl;
}
