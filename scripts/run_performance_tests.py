#!/usr/bin/env python3

import argparse
import multiprocessing
import os
import pathlib
import platform
import signal
import socket
import subprocess
import sys
import time

from performance_utils import *

cpu_count = multiprocessing.cpu_count ()

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + "/../")

exe_dir_non_pgo  = os.path.join (base_dir, "build",
                            platform.processor (), "bin")

exe_dir_pgo = os.path.join (base_dir, "build",
                            platform.processor () + ".pgo_release", "bin")

parser = argparse.ArgumentParser (description="Latency testing")

# note only allowing one shared memory name
parser.add_argument ("--name", required=True, help="shared memory name to use")
parser.add_argument ("--log_level", default="INFO",
                    choices=["TRACE", "DEBUG", "INFO", "WARNING",
                            "ERROR", "FATAL"],
                    help="Set the logging level")
parser.add_argument ("--timeout", required=True, help="run time (secs)")

parser.add_argument ("--server_cpu",  type=int, default=-1,
                    help="bind spmc server to cpu id")
parser.add_argument ("--server_queue_size",   required=True, help="queue size (bytes)")
parser.add_argument ("--server_message_size", required=True, help="message size (bytes)")
parser.add_argument ("--server_rate", default=0,
                    help="target messages per second (default is maximum)")
parser.add_argument ("--server_pgo", default=False, action="store_true",
                    help="Use profile guided optimised server binary")

parser.add_argument ("--client_cpu_list", nargs='+', type=int, default=-1,
                    help="bind spmc client(s) to cpu id list, "
                         "can be fewer than the total number of clients")
parser.add_argument ("--client_count",  type=int, required=True,
                    help="Number of consumer clients")
parser.add_argument ("--client_stats", nargs='+', default=["latency,throughput"],
                    choices=["interval", "latency", "throughput"],
                    help="select statistics for output")
parser.add_argument ("--client_directory", required=False,
                    help="set output directory to write stats to file")
parser.add_argument ("--client_prefetch_size", required=False, default=0,
                    help="client prefetch size")
parser.add_argument ("--client_pgo", default=False, action="store_true",
                    help="Use profile guided optimised client binary")

args = parser.parse_args ()

server_exe_dir = exe_dir_pgo if args.server_pgo is True else exe_dir_non_pgo
server_exe     = os.path.join (server_exe_dir, "spmc_server")

client_exe_dir = exe_dir_pgo if args.client_pgo is True else exe_dir_non_pgo
client_exe     = os.path.join (client_exe_dir, "spmc_client")

print ("host_name:           " + socket.gethostname ())
print ("cpu_count:           " + str (cpu_count))
print ("arch:                " + str (platform.processor ()))
print ("name:                " + args.name)
print ("run_time:            " + str (args.timeout) + " seconds")
print ("log_level:           " + args.log_level)
print ("")
print ("server_exe           " + server_exe)
print ("server_cpu:          " + str (args.server_cpu))
print ("server_queue_size:   " + str (args.server_queue_size) + " bytes")
print ("server_message_size: " + str (args.server_message_size) + " bytes")
print ("server_rate:         " + str (args.server_rate) + " messages/second")
print ("")
print ("client_exe:          " + client_exe)
print ("client_count:        " + str (args.client_count))
print ("client_cpu_list:     " + ' '.join (map (str, args.client_cpu_list)))
print ("client_stats:        " + ' '.join (args.client_stats))
print ("client_prefetch_size:  " + str(args.client_prefetch_size))

client_cpu_list = cpu_bind_list (args.client_cpu_list, args.client_count)

directory = None

if args.client_directory is not None:
    directory = output_directory (args.client_directory,
                                  args.server_rate,
                                  args.server_message_size,
                                  args.client_count)

    print ("client_directory:    " + str (directory))
    print ("")

# Delete shared memory if it exists
subprocess.check_call ([pathlib.Path (exe_dir_non_pgo) / "remove_shared_memory",
                        "--names", args.name])

# Create the server command
server_cmd = [server_exe,
    "--cpu",          str (args.server_cpu),
    "--name",         args.name,
    "--message_size", str (args.server_message_size),
    "--queue_size",   str (args.server_queue_size),
    "--rate",         str (args.server_rate),
    "--log_level",    args.log_level]

# Run the server
print ('%s' % ' '.join (map (str, server_cmd)))

server = subprocess.Popen (server_cmd, stdout=subprocess.PIPE)

# Allow the server time to start, then check if it is running
time.sleep (1)
poll = server.poll ()
if poll is not None:
    print ("spmc_server did not run")
    server.send_signal (signal.SIGINT)
    exit (1)

# Run the client(s)
clients = []

stats = ','.join (args.client_stats)

for client_index in range (args.client_count):
    client_cmd = [client_exe,
                "--name",     args.name,
                "--log_level", "INFO"]

    cpu_bind = client_cpu_list.pop (0)

    if cpu_bind != -1:
        client_cmd.extend (["--cpu", str (cpu_bind)])

    if (client_index == (args.client_count -1)):
        client_cmd.extend (["--stats", stats])

        if (directory):
            client_cmd.extend (["--directory", directory])

        if (args.client_prefetch_size != 0):
            client_cmd.extend (["--prefetch_size", str (args.client_prefetch_size)])

    print ('%s' % ' '.join (map (str, client_cmd)))

    clients.append (subprocess.Popen (client_cmd))

time.sleep (int (args.timeout))

for client in clients:
    client.send_signal (signal.SIGINT)
    client.wait ()
    print ("client exited")

server.send_signal (signal.SIGINT)
server.wait ()
print ("server exited")

subprocess.check_call ([os.path.join (exe_dir_non_pgo, "remove_shared_memory"),
                        "--names", args.name])

print ("Latency tests finished")