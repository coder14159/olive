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
parser.add_argument ("--client_cpu_list", nargs='+', type=int, default=[],
                    help="Bind client(s) to cpu id list. "
                         "Can be fewer than the total number of clients")

args = parser.parse_args ()

if len (args.client_stats) is 1 and args.client_stats[0] == 'interval':
  sys.exit ('Error: Argument --client_stats option requires "latency and/or" ' +
            'throughput" to be included')

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
###############################################################################
print ("# Build standard release spmc_client and spmc_server")
utils.build_executable ("spmc_client",
                        utils.BuildType.RELEASE, args.jobs)
utils.build_executable ("spmc_server",
                        utils.BuildType.RELEASE, args.jobs)

###############################################################################
print ("# Build Profile Guided Optimisation (PGO) binaries")

print ("# Build/run client and server genarating PGO data")
utils.build_executable ("spmc_client",
                        utils.BuildType.PGO_PROFILE, args.jobs)
utils.build_executable ("spmc_server",
                        utils.BuildType.PGO_PROFILE, args.jobs)

print ("# Run SPMC server [BuildType=PGO_PROFILE]")
server = utils.run_spmc_server (args, utils.BuildType.PGO_PROFILE)

print ("# Start SPMC clients [BuildType=PGO_PROFILE]")
clients = utils.run_spmc_clients (args, utils.BuildType.PGO_PROFILE)

print ("# Monitor the running clients and exit after a configured period")
utils.monitor_server_and_clients (args, clients, server)

print ("# Remove the named shared memory")
utils.execute ([str (utils.sub_dir_path_bin (
                      utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])

###############################################################################
print ("# Build/run client and server utilising the PGO data")

utils.build_executable ("spmc_client",
                        utils.BuildType.PGO_RELEASE, args.jobs)
utils.build_executable ("spmc_server",
                        utils.BuildType.PGO_RELEASE, args.jobs)

print ("# Run SPMC server [BuildType=PGO_RELEASE]")
server = utils.run_spmc_server (args, utils.BuildType.PGO_RELEASE)

print ("# Start SPMC clients [BuildType=PGO_RELEASE]")
clients = utils.run_spmc_clients (args, utils.BuildType.PGO_RELEASE)

print ("# Monitor the running clients and exit after a configured period")
utils.monitor_server_and_clients (args, clients, server)

print ("# Remove the named shared memory")
utils.execute ([str (utils.sub_dir_path_bin (
                  utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])
