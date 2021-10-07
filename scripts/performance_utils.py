#!/usr/bin/env python3

from enum import Enum
from pathlib import Path
from subprocess import Popen, PIPE, STDOUT, CalledProcessError

import platform
import subprocess
import os
import signal
import sys
import time
import datetime

#
# Print a line of text
#
def print_line (line):
  if len (line) > 0:
    if type (line) == str:
      print (line)
    else:
      # utf-8 removes byte code markup strip removes additional newline
      print (line.decode ('ASCII').strip ())
#
# Enum of build types
#
class BuildType (Enum):
  RELEASE     = 1
  DEBUG       = 2
  PGO_PROFILE = 3
  PGO_RELEASE = 4
#
# Return the build directory suffix for different build types
#
def build_type_dir (build_type):
  build_suffix_path = Path (platform.processor ())

  build_type_names = {
    BuildType.RELEASE:      "",
    BuildType.DEBUG:        ".debug",
    BuildType.PGO_PROFILE:  ".pgo_profile",
    BuildType.PGO_RELEASE:  ".pgo_release",
  }

  return platform.processor () + build_type_names.get (build_type)

#
# Get the root path of this project
#
def base_path ():
  return Path (__file__).parent.parent

#
# Return the sub-directory path of a build type
#
def sub_dir_path_build (build_type):
  return Path ("build") / build_type_dir (build_type)

#
# Return the sub-directory path to the bin directory
#
def sub_dir_path_bin (build_type):
  return sub_dir_path_build (build_type) / "bin"

#
# Return the makefile macro for building different build types
#
def build_type_flag (build_type):
  switch = {
    BuildType.RELEASE:     'RELEASE=1',
    BuildType.DEBUG:       'DEBUG=1',
    BuildType.PGO_PROFILE: 'PGO_PROFILE=1',
    BuildType.PGO_RELEASE: 'PGO_RELEASE=1'
  }
  return switch.get (build_type, "Invalid build_type")

#
# Return a human readable string for message throughput
#
def throughput_to_pretty (rate):
  if rate == 'max':
      return rate
  if rate == '0':
      return 'max'

  K = 1.0e3
  M = 1.0e6
  G = 1.0e6

  float_rate = float (rate)

  if float_rate >= G:
      return str (round (float_rate / G, 1)) + " G msgs/s"
  if float_rate >= M:
      return str (round (float_rate / M, 1)) + " M msgs/s"
  if float_rate >= K:
      return str (round (float_rate / K, 1)) + " K msgs/s"

  return str (round (float_rate, 0)) + " msgs/s"


#
# Invoke a command described by the command_list and wait until is completes
#
def check_call (command_list):
  print ("$ " + ' '.join (command_list))

  subprocess.check_call (command_list,  cwd=str (base_path ()))

#
# Invoke a command in a shell using the information in the command_list
#
# Returns a Popen object
#
def execute (command_list):
    print ('$ ' + ' '.join (command_list))

    return Popen (' '.join (command_list), stdout=PIPE, stderr=PIPE, shell=True)

#
# Read output from a process and return when a specific string is output to cout
#
def wait_for_stdout_string (process, target_string):

  while process.poll () is None:
    line = process.stdout.readline ()

    print_line (line)

    if target_string in str (line):
      break
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
  client_count, client_prefetch_size = None):

  server_rate_str = 'max' if server_rate is '0' else str (server_rate)

  path = Path (base_directory) \
            / 'server_queue_size'    / str (server_queue_size) \
            / 'server_rate'          / server_rate_str  \
            / 'server_message_size'  / str (server_message_size) \
            / 'client_count'         / str (client_count)

  if client_prefetch_size is not None:
    path /= Path ('client_prefetch_size') / str (client_prefetch_size)

  return path

#
# Build an executable
#
def build_executable (exe_name, build_type, jobs):
  initial_dir = os.getcwd ()

  os.chdir (str (base_path ()))

  check_call (["make", "-j" + str (jobs), build_type_flag (build_type),
    str (sub_dir_path_bin (build_type) / exe_name)])

  os.chdir (initial_dir)

def build_run_spmc_server (args, build_type):
  print ("# Build spmc_server")

  exe_name = "spmc_server"

  build_executable (exe_name, build_type, args.jobs)

  print ("# Run the spmc_server")
  server = execute ([str (sub_dir_path_bin (build_type) / exe_name),
              "--log_level", str (args.log_level),
              "--name", args.memory_name,
              "--cpu", str (args.server_cpu),
              "--message_size", str (args.server_message_size),
              "--queue_size", str (args.server_queue_size),
              "--rate", str (args.server_rate)])

  # Wait until the server is ready
  wait_for_stdout_string (server, "Found or created queue")

  server_rate_str = 'max' if args.server_rate is '0' else str (args.server_rate)

  return server

#
# Run the SPMC clients and return an array of the running clients
#
def build_run_spmc_clients (args, build_type):
  assert int (args.client_count) >= 1, "client count should be at least 1"

  print ("# Run " + str (args.client_count) + " SPMC clients")

  exe_name = "spmc_client"

  cpu_list = args.client_cpu_list

  clients = []

  build_executable (exe_name, build_type, args.jobs)

  client_exe_path = sub_dir_path_bin (build_type) / exe_name

  # Run the clients
  for count in range (0, int (args.client_count)):

    log_level = args.log_level if count is int (args.client_count) -1 \
                               else "ERROR"

    cpu = NO_CPU_BIND if len (args.client_cpu_list) == 0 \
                      else args.client_cpu_list.pop ()

    client = execute ([str (client_exe_path),
          "--name", args.memory_name,
          "--stats", ','.join (args.client_stats),
          "--cpu",  str (cpu),
          "--log_level", log_level])

    clients.append (client)

  assert len (clients) == int (args.client_count), \
    "Incorrect number of clients started: " + str (clients)

  return clients

#
# Output logging of a client a list of clients and exit after a set time period
#
def monitor_server_and_clients (args, clients, server):
  logging_client = clients[-1]

  start = time.monotonic ()

  # Run the clients
  while True:
    client_output = logging_client.stdout.readline ()

    print_line (client_output)

    if (time.monotonic () - start) > float (args.run_time):
      server.send_signal (signal.SIGINT)
      server.wait ()

      for client in clients:
        client.send_signal (signal.SIGINT)

    if not client_output:
      break

  return clients

