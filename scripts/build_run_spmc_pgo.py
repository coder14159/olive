#!/usr/bin/env python3

import argparse
import datetime
import multiprocessing
import os
import pathlib
import platform
import signal
import socket
import subprocess
import sys
import time

from time import sleep

from enum import Enum
from subprocess import Popen, PIPE, STDOUT, CalledProcessError

import performance_utils as utils
from pathlib import Path

# #========================================
# # Testing
# cpu_count = multiprocessing.cpu_count ()

# script_dir = Path (__file__).parent
# print (str (script_dir))

# base_dir = Path (script_dir).parent
# print (str (base_dir))

# print ("base_path: " + str (utils.base_path ()))

# print (str (utils.sub_dir_path_bin (utils.BuildType.PGO_PROFILE)))

# print (str (utils.sub_dir_path_bin (utils.BuildType.PGO_PROFILE) / "my_exe"))
# print (str (utils.sub_dir_path_bin (utils.BuildType.PGO_RELEASE) / "my_exe"))
# print (str (utils.sub_dir_path_bin (utils.BuildType.DEBUG)/ "my_exe"))
# print (str (utils.sub_dir_path_bin (utils.BuildType.RELEASE)/ "my_exe"))

# #========================================

parser = argparse.ArgumentParser (
  description="Generate Profile Guided Optimised (PGO) binaries")

parser.add_argument ("--jobs", required=False, default='2',
                    help="Build job count")
parser.add_argument ("--log_level", default="INFO",
                    choices=["TRACE", "DEBUG", "INFO", "WARNING",
                            "ERROR", "FATAL"],
                    help="Set the logging level")
parser.add_argument ("--run_time", required=True, help="Run time (seconds)")
parser.add_argument ("--memory_name", required=False, default='spmc_pgo',
                    help="Name of shared memory")

# Server configuration
parser.add_argument ("--server_cpu",  type=int, default=-1,
                    help="Bind server to cpu id")
parser.add_argument ("--server_queue_size", required=True,
                    help="Queue size (bytes)")
parser.add_argument ("--server_message_size", required=True,
                    help="Message size (bytes)")
parser.add_argument ("--server_rate", default=0,
                    help="Target messages per second rates. "
                         "Use \"0\" for maximum throughput")

# Client configuration
parser.add_argument ("--client_count", default=1,
                    help="Number of clients to run")
parser.add_argument ("--client_stats", nargs='+',
                    default=["latency,throughput,interval"],
                    choices=["latency", "throughput", "interval"],
                    help="Select statistics for output")
parser.add_argument ("--client_cpu_list", nargs='+', type=int, default=-1,
                    help="Bind client(s) to cpu id list. "
                         "Can be fewer than the total number of clients")

args = parser.parse_args ()

if len (args.client_stats) is 1 and args.client_stats[0] == 'interval':
  print ('If using client stats "latency" and/or "throughput" must be included')
  exit ()

print ("host_name:              " + socket.gethostname ())
print ("cpu_count:              " + str (multiprocessing.cpu_count ()))
print ("arch:                   " + str (platform.processor ()))
print ("jobs:                   " + str (args.jobs))
print ("run_time:               " + str (args.run_time) + " seconds")
print ("log_level:              " + args.log_level)
print ("memory_name:            " + args.memory_name)
print ("server_queue_size:      " + args.server_queue_size)
print ("base_dir:               " + str (utils.base_path ()))
print ("")
if args.server_cpu:
  print ("server_cpu:             " + str (args.server_cpu))
else:
  print ("server_cpu:             None")
print ("server_message_size:    " + str (args.server_message_size) + " bytes")
print ("server_rate:            " + str (args.server_rate) + " msgs/sec")
print ("")
print ("client_count:           " + str (args.client_count))
if args.client_stats is None:
  print ("client_stats:             None")
else:
  print ("client_stats:           " + ' '.join (args.client_stats))

if args.client_cpu_list is None:
  print ("client_cpu_list:        None")
else:
  print ("client_cpu_list:        " + ' '.join (map (str, args.client_cpu_list)))
print ("")

utils.build_executable ("remove_shared_memory",
                        utils.BuildType.RELEASE, args.jobs)

print ("# Delete the named test shared memory if it exists")
utils.execute ([str (utils.sub_dir_path_bin (
                      utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])

# utils.build_executable ("spmc_client",
#                         utils.BuildType.PGO_PROFILE, args.jobs)
# utils.build_executable ("spmc_server",
#                         utils.BuildType.PGO_PROFILE, args.jobs)

print ("# Generate Profile Guided Optimisation (PGO) data")

print ("# Run SPMC server [BuildType=PGO_PROFILE]")
server = utils.build_run_spmc_server (args, utils.BuildType.PGO_PROFILE)

print ("# Start the PGO clients [BuildType=PGO_PROFILE]")
clients = utils.build_run_spmc_clients (args, utils.BuildType.PGO_PROFILE)

print ("# Monitor the running clients and exit after a configured period")
utils.monitor_server_and_clients (args, clients, server)

print ("# Remove the named shared memory")
utils.execute ([str (utils.sub_dir_path_bin (
                      utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])
###############################################################################
print ("# Generate binaries utilising the PGO data")

print ("# Run SPMC server [BuildType=PGO_RELEASE]")
server = utils.build_run_spmc_server (args, utils.BuildType.PGO_RELEASE)

print ("# Start the PGO clients [BuildType=PGO_RELEASE]")
clients = utils.build_run_spmc_clients (args, utils.BuildType.PGO_RELEASE)

print ("# Monitor the running clients and exit after a configured period")
utils.monitor_server_and_clients (args, clients, server)

print ("# Remove the named shared memory")
utils.execute ([str (utils.sub_dir_path_bin (
                      utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])
exit ()
# Build binaries for profile guided optimisation
pgo_profile_dir = utils.sub_dir_path_bin (utils.BuildType.PGO_PROFILE)

print ("# Build client/server binaries for performance profiling")

utils.build_client_server_executables (utils.BuildType.PGO_PROFILE, jobs=2)

print ("# Generate profile guided data")

print ("# Run the server")

server = utils.run_spmc_server (args)

# server = utils.execute ([str (pgo_profile_dir / "spmc_server"),
#                "--log_level", str (args.log_level),
#                "--name", args.memory_name,
#                "--cpu", str (args.server_cpu),
#                "--message_size", str (args.server_message_size),
#                "--queue_size", str (args.server_queue_size),
#                "--rate", str (args.server_rate)])

# Wait until the server is ready for client connections
utils.wait_for_stdout_string (server, "Found or created queue")

print ("# Run the clients")

utils.run_spmc_clients (args.client_count)

clients = utils.execute ([str (pgo_profile_dir / "spmc_client"),
               "--log_level", args.log_level,
               "--name", args.memory_name,
               "--cpu", str (args.client_cpu_list.pop ()),
               "--stats", ','.join (args.client_stats)])

start = time.monotonic ()

while True:
  client_output = client.stdout.readline ()

  utils.print_line (client_output)

  if (time.monotonic () - start) > float (args.run_time):
    server.send_signal (signal.SIGINT)
    server.wait ()
    print ("# Server to exited")

    for client in clients:
      client.send_signal (signal.SIGINT)
      client.wait ()
      print ("# Client to exited")

exit ()

print ("# Generate PGO optimisation data for spmc_client and spmc_server")
print ("# Run PGO spmc_server")
print ("# memory name: " + args.memory_name)

print ("# Run PGO spmc_server")
server_cmd = [os.path.join (base_dir, spmc_server_path_pgo),
                  "--log_level", args.log_level,
                  "--cpu", str ("1"),
                  "--message_size", str (args.server_message_size),
                  "--name", str (args.memory_name),
                  "--queue_size", str (args.server_queue_size),
                  "--rate", str (args.server_rate)]

# print ('%s' % ' '.join (map (str, server_cmd)))
server = utils.execute (server_cmd)
client = None

# Allow the server time to initialise, then check if it is running
time.sleep (1)

if server.poll () is not None:
    print ("spmc_server did not run")
    server.send_signal (signal.SIGINT)

print ("# Run the spmc_clients")

for i in range (1, int (args.client_count) + 1):
  print ("client: " + str (i))
  client = subprocess.Popen (client_cmd, stdout=PIPE, stderr=PIPE)

start = time.time ()
# while (time.time () - start) <
# time.sleep (int (args.timeout))


# client = utils.execute ([os.path.join (base_dir, spmc_client_path),
#                   # "--cpu", "1",
#                   # "--message_size", str (args.server_message_size),
#                   "--name", str (args.memory_name),
#                   "--log_level", args.log_level,
#                   "--stats", ','.join (str (x) for x in args.client_stats)])

# while True:
#   if server.poll () is None:
#     line = server.stderr.readline ()

#     if line:
#       print ("err: " + str (line.strip ()))
#     line = server.stdout.readline ()
#     if line:
#       print ("out: " + line.strip ())

# server = subprocess.Popen ([str (spmc_server_path),
#                         '--cpu', '1',
#                         '--message_size', str (args.server_message_size),
#                         '--name', str (args.memory_name),
#                         '--queue_size', str (args.queue_size),
#                         '--rate', str (args.server_rate)])



print ("# call ended")
exit ()


# output = ''
# for line in server.stdout:
#   line = line.decode(encoding=sys.stdout.encoding,
#         errors='replace' if (sys.version_info) < (3, 5)
#         else 'backslashreplace').rstrip()
#   print (line)

# time.sleep (1)

# print ("# Run PGO spmc_client")
# client = subprocess.Popen ([str (spmc_client_path),
#                         "--name", args.memory_name,
#                         "--log_level", "INFO",
#                         "--stats", "interval,latency,throughput"], shell=True)

time.sleep (5)

server.terminate ()
# client.terminate ()



exit ()

build = subprocess.Popen (build_cmd, stdout=subprocess.PIPE)

poll = build.poll ()
if poll is not None:
    print ("build did not run")
    build.send_signal (signal.SIGINT)
    exit ()

build.wait ()

print ("build complete")

exit ()

print ("script path: " + utils.get_script_path ())

# Relative path to executable from the root of the repository
exe_path = "build/" + str (platform.processor ()) + "/bin"
print ("exe_path: " + exe_path)

print ("cd " + base_dir + " && make " + str (exe_path / "remove_shared_memory"))

subprocess.Popen ("make " + str (exe_path / "remove_shared_memory"))
