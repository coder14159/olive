#include <exception>
#include <iostream>
#include <string>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/log/trivial.hpp>

#include <cxxopts.hpp>

namespace ip = boost::interprocess;

 int main(int argc, char* argv[]) try
{
  const int SUCCESS = 0;
  const int ERROR   = 1;

  cxxopts::Options options ("remove_shared_memory",
                            "Delete named shared memory instance");

  options.add_options ()
    ("h,help", "Remove named shared memory instance from the local machine")
    ("n,name", "Shared memory name", cxxopts::value<std::string> ());

  auto result = options.parse (argc, argv);

  if (result.count ("help") != 0)
  {
    std::cout << options.help () << std::endl;
    return SUCCESS;
  }

  if (result.count ("name") != 1)
  {
    std::cerr << "Set a value for shared memory name\n" << std::endl;
    return ERROR;
  }

  auto name = result["name"].as<std::string> ();


  if (ip::shared_memory_object::remove (name.c_str ()))
  {
    std::cout << "Shared memory removed. Name: " << name << std::endl;
  }
  else
  {
    std::cout << "Shared memory NOT removed. Name: " << name << std::endl;
  }

  return SUCCESS;
}
catch (const std::exception &e)
{
  std::cerr << e.what () << std::endl;
}
