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

# Return a unique path if directory path already exists
def directory_path (directory):
    path = directory
    i = 0
    while pathlib.Path (path).exists ():
        i = i + 1
        path = pathlib.Path (directory) / ("v" + str (i))

    return path

cpu_count = multiprocessing.cpu_count ()

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + "/../")
exe_dir = os.path.join (base_dir, "build", platform.processor (), "bin")

parser = argparse.ArgumentParser (description="Latency testing")

# note only allowing one shared memory name
parser.add_argument ("--name", help="shared memory name to delete")

parser.add_argument ("--log_level", default="INFO",
                    choices=["TRACE", "DEBUG", "INFO", "WARNING",
                            "ERROR", "FATAL"],
                    help="Set the logging level")
parser.add_argument ("--timeout", default=10, help="run time (secs)")

parser.add_argument ("--server_cpu",  type=int, default=-1,
                    help="bind spmc server to cpu id")
parser.add_argument ("--server_queue_size",   help="queue size (bytes)")
parser.add_argument ("--server_message_size", help="message size (bytes)")
parser.add_argument ("--server_rate",         help="target messages per second")

parser.add_argument ("--client_cpu_list", nargs='+', type=int, default=-1,
                    help="bind spmc client(s) to cpu id list, "
                         "can be fewer than the total number of clients")
parser.add_argument ("--client_count",  type=int,
                    help="Number of consumer clients")
parser.add_argument ("--client_stats", nargs='+', default="",
                    choices=["interval", "latency", "throughput"],
                    help="select statistics for output")
parser.add_argument ("--client_directory",
                    help="set output directory to write stats to file")

args = parser.parse_args ()

print ("hostname:            " + socket.gethostname ())
print ("CPU count:           " + str (cpu_count))
print ("exe_dir:             " + exe_dir)
print ("arch:                " + str (platform.processor ()))
print ("name:                " + args.name)
print ("timeout:             " + str (args.timeout) + " seconds")
print ("log_level:           " + args.log_level)
print ("")
print ("server_cpu:          " + str (args.server_cpu))
print ("server_queue_size:   " + str (args.server_queue_size) + " bytes")
print ("server_message_size: " + str (args.server_message_size) + " bytes")
print ("server_rate:         " + str (args.server_rate) + " messages/second")
print ("")
print ("client_count:        " + str (args.client_count))
print ("client_cpu_list:     " + ' '.join (map (str, args.client_cpu_list)))
print ("client_stats:        " + ' '.join (args.client_stats))

directory = pathlib.Path (args.client_directory)    \
    / "server_rate" / str (args.server_rate)        \
    / "client_count" / str (args.client_count)      \
    / "message_size" / str (args.server_message_size)

directory = directory_path (directory)

print ("client_directory:    " + str (directory))
print ("")

# Delete shared memory if it exists
subprocess.check_call ([pathlib.Path (exe_dir) / "remove_shared_memory",
                        "--names", args.name])

# Create the server command
server_cmd = [
    os.path.join (exe_dir, "spmc_server"),
    "--cpu",         str (args.server_cpu),
    "--name",        args.name,
    "--messagesize", str (args.server_message_size),
    "--queuesize",   str (args.server_queue_size),
    "--rate",        str (args.server_rate),
    "--loglevel",    args.log_level]

# Run the server
server = subprocess.Popen (server_cmd, stdout=subprocess.PIPE)

# Allow the server time to start, then check if it is running
time.sleep (1)
poll = server.poll ()
if poll is not None:
    print ("server did not run")
    server.send_signal (signal.SIGINT)
    exit (1)

# Run the client(s)
clients = []

stats = ','.join (args.client_stats)

for client_index in range (args.client_count):
    client_cmd = [os.path.join (exe_dir, "spmc_client"),
                "--name",     args.name,
                "--loglevel", "INFO"]

    # Client processes bound to a CPU they should be the last to start
    # Bind client to a CPU where requested
    if (args.client_count - client_index -1) in args.client_cpu_list:
        client_cmd.extend (["--cpu", str (args.client_count - client_index -1)])

    if (client_index == (args.client_count -1)):
        client_cmd.extend (["--stats", stats])
        if (directory):
            client_cmd.extend (["--directory", directory])

    print ('[DEBUG] %s' % ' '.join (map (str, client_cmd)))
    clients.append (subprocess.Popen (client_cmd, stdout=subprocess.PIPE))

time.sleep (int (args.timeout))

for client in clients:
    client.send_signal (signal.SIGINT)
    client.wait ()
    print ("client exited")

server.send_signal (signal.SIGINT)
server.wait ()
print ("server exited")

subprocess.check_call ([os.path.join (exe_dir, "remove_shared_memory"),
                        "--names", args.name])

print ("Latency tests finished")


