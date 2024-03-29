#!/usr/bin/env python3

import argparse
import gc
import multiprocessing
import platform
import socket
import sys

import build_run_spmc_pgo_utils as utils

gc.disable ()

parser = argparse.ArgumentParser (
  description="Generate Profile Guided Optimised (PGO) binaries")

parser.add_argument ("--build_jobs", required=False, default='2',
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

if len (args.client_stats) == 1 and args.client_stats[0] == 'interval':
  sys.exit ('Error: Argument --client_stats option requires "latency and/or" ' +
            'throughput" to be included')

print ("host_name:              " + socket.gethostname ())
print ("cpu_count:              " + str (multiprocessing.cpu_count ()))
print ("arch:                   " + str (platform.processor ()))
print ("build_jobs:             " + str (args.build_jobs))
print ("run_time:               " + str (args.run_time) + " seconds")
print ("log_level:              " + args.log_level)
print ("memory_name:            " + args.memory_name)
print ("server_queue_size:      " + args.server_queue_size + " bytes")
print ("base_dir:               " + str (utils.base_path ()))
print ("")

cpu = "-" if args.server_cpu == -1 else args.server_cpu
print ("server_cpu:             " + str (cpu))
print ("server_message_size:    " + str (args.server_message_size) + " bytes")

print ("server_rate:            " + utils.throughput_to_pretty (args.server_rate))
print ("")

client_cpu_list = None if args.client_cpu_list is None else \
                          ' '.join (map (str, args.client_cpu_list))
print ("client_cpu_list:        " + client_cpu_list)
print ("client_count:           " + str (args.client_count))

stats = 'None' if args.client_stats is None else  ' '.join (args.client_stats)
print ("client_stats:           " + stats)

print ("")

print ("# Build standard release spmc_client and spmc_server")
utils.build_executable ("remove_shared_memory",
                        utils.BuildType.RELEASE, args.build_jobs)

print ("\n# Delete the named test shared memory if it exists")
utils.execute ([str (utils.sub_dir_path_bin (
                      utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])
###############################################################################
print ("\n# Build standard release spmc_client and spmc_server")
utils.build_executable ("spmc_client",
                        utils.BuildType.RELEASE, args.build_jobs)
utils.build_executable ("spmc_server",
                        utils.BuildType.RELEASE, args.build_jobs)

###############################################################################
print ("\n# Build Profile Guided Optimisation (PGO) binaries")

print ("# Build/run client and server genarating PGO data")
utils.build_executable ("spmc_client",
                        utils.BuildType.PGO_PROFILE, args.build_jobs)
utils.build_executable ("spmc_server",
                        utils.BuildType.PGO_PROFILE, args.build_jobs)

print ("\n# Run SPMC server [BuildType=PGO_PROFILE]")
server = utils.run_spmc_server (args, utils.BuildType.PGO_PROFILE)

print ("\n# Start SPMC clients [BuildType=PGO_PROFILE]")
clients = utils.run_spmc_clients (args, utils.BuildType.PGO_PROFILE)

print ("\n# Monitor the running clients and exit after a configured period")
utils.monitor_server_and_clients (args, clients, server)

print ("\n# Remove the named shared memory")
utils.execute ([str (utils.sub_dir_path_bin (
                      utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])

###############################################################################
print ("\n# Build/run client and server utilising the PGO data")

utils.build_executable ("spmc_client",
                        utils.BuildType.PGO_RELEASE, args.build_jobs)
utils.build_executable ("spmc_server",
                        utils.BuildType.PGO_RELEASE, args.build_jobs)

print ("\n# Run SPMC server [BuildType=PGO_RELEASE]")
server = utils.run_spmc_server (args, utils.BuildType.PGO_RELEASE)

print ("\n# Start SPMC clients [BuildType=PGO_RELEASE]")
clients = utils.run_spmc_clients (args, utils.BuildType.PGO_RELEASE)

print ("\n# Monitor the running clients and exit after a configured period")
utils.monitor_server_and_clients (args, clients, server)

print ("\n# Remove the named shared memory")
utils.execute ([str (utils.sub_dir_path_bin (
                  utils.BuildType.RELEASE) / "remove_shared_memory"),
                "--names", args.memory_name])

gc.enable ()