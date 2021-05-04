#!/usr/bin/env python3

import pathlib

#
# Update the CPU bind list to use -1 for clients which do not bind to a cpu
#
NO_CPU_BIND = -1

def cpu_bind_list (cpu_list, client_count):

  while len (cpu_list) < client_count:
    cpu_list.insert (0, NO_CPU_BIND)

  return cpu_list

#
# Return a unique path if directory path already exists
#
def output_directory (base_directory, server_rate, message_size, client_count):

  base_directory = pathlib.Path (base_directory)       \
            / "server_rate"  / str (server_rate)  \
            / "message_size" / str (message_size) \
            / "client_count" / str (client_count)

  i = 0
  directory = base_directory
  while pathlib.Path (directory).exists ():
      i = i + 1
      directory = pathlib.Path (base_directory) / ("v" + str (i))

  return directory

