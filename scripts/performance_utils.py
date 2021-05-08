#!/usr/bin/env python3

from pathlib import Path

#
# Update the CPU bind list to use -1 for clients which do not bind to a cpu
#
NO_CPU_BIND = -1

def cpu_bind_list (cpu_list, client_count):

  while len (cpu_list) < client_count:
    cpu_list.insert (0, NO_CPU_BIND)

  return cpu_list

# Return a directory based on the parameters supplied
def output_directory_path (
  base_directory,
  server_queue_size, server_rate, server_message_size,
  client_count, client_prefetch_size):

  server_rate_str = 'max' if server_rate is '0' else str (server_rate)

  return Path (base_directory) \
            / 'server_queue_size'    / str (server_queue_size) \
            / 'server_rate'          / server_rate_str  \
            / 'server_message_size'  / str (server_message_size) \
            / 'client_count'         / str (client_count) \
            / 'client_prefetch_size' / str (client_prefetch_size)

# Return a human readable message throughput string
def throughput_messages_to_pretty (rate):
    if rate == 'max':
        return rate
    if rate == '0':
        return 'max'

    K = 1.0e3
    M = 1.0e6
    G = 1.0e6

    float_rate = float (rate)

    if float_rate > G:
        return str (round (float_rate / G, 1)) + " G msgs/s"
    if float_rate > M:
        return str (round (float_rate / M, 1)) + " M msgs/s"
    if float_rate > K:
        return str (round (float_rate / K)) + " K msgs/s"

    return str (round (float_rate)) + " msgs/s"