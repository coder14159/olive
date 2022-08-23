#!/usr/bin/env python3

import argparse
import gc
import multiprocessing
import os
import pathlib
import platform
import signal
import socket
import subprocess
import time

import build_run_spmc_pgo_utils as utils

cpu_count = multiprocessing.cpu_count ()

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + "/../")

exe_dir  = os.path.join (base_dir, "build",
                            platform.processor (), "bin")

exe_dir_pgo = os.path.join (base_dir, "build",
                            platform.processor () + ".pgo_release", "bin")

parser = argparse.ArgumentParser (description="Latency testing")

# note only allowing one shared memory name
parser.add_argument ("--tool_type", choices=['spmc', 'spsc'], required=True,
                    help="The tool type to use")
parser.add_argument ("--memory_name", required=True,
                    help="Provide a name for shared memory")
parser.add_argument ("--directory_name", required=False,
                    help="Optional directory name")
parser.add_argument ("--log_level", default="INFO",
                    choices=["TRACE", "DEBUG", "INFO", "WARNING",
                             "ERROR", "FATAL"],
                    help="Set the logging level")
parser.add_argument ("--timeout", required=True, help="Run time (seconds)")

# Server configuration
parser.add_argument ("--server_cpu",  type=int, default=-1,
                    help="Bind server to cpu id")
parser.add_argument ("--server_queue_size_list", required=True, nargs='+',
                    help="Queue size (bytes)")
parser.add_argument ("--server_message_size", required=True, help="Message size (bytes)")
parser.add_argument ("--server_rate_list", default=0, nargs='+',
                    help="Target messages per second rates. Maximum throughput is \"0\"")
parser.add_argument ("--server_pgo", default=False, action="store_true",
                    help="Use profile guided optimised server binary")

# Client configuration
parser.add_argument ("--client_cpu_list", nargs='+', type=int, default=[],
                    help="Bind client(s) to cpu id list. "
                         "List may hold fewer values than the total number of clients")
parser.add_argument ("--client_count_list",  required=True, nargs='+',
                    help="Number of consumer clients")
parser.add_argument ("--client_stats", nargs='+', default=["latency,throughput"],
                    choices=["latency", "throughput", "interval"],
                    help="Select statistics for output")
parser.add_argument ("--client_directory", required=False,
                    help="Set output directory to write stats to file")
parser.add_argument ("--client_pgo", default=False, action="store_true",
                    help="Use profile guided optimised client binary")

args = parser.parse_args ()

server_exe_dir = exe_dir_pgo if args.server_pgo == True else exe_dir
server_exe     = os.path.join (server_exe_dir, args.tool_type + '_server')

client_exe_dir = exe_dir_pgo if args.client_pgo == True else exe_dir
client_exe     = os.path.join (client_exe_dir, args.tool_type + '_client')

print ("host_name:              " + socket.gethostname ())
print ("cpu_count:              " + str (cpu_count))
print ("arch:                   " + str (platform.processor ()))
print ("run_time:               " + str (args.timeout) + " seconds")
print ("log_level:              " + args.log_level)
print ("memory_name:            " + args.memory_name)
print ("")
print ("server_exe              " + server_exe)
print ("server_cpu:             " + (str ("") if args.server_cpu == -1 else str (args.server_cpu)))
print ("server_queue_size_list: " + ','.join (map (str, args.server_queue_size_list)))
print ("server_message_size:    " + str (args.server_message_size) + " bytes")
print ("server_rate_list:       " + ','.join (map (str, args.server_rate_list))
                                  + ' msgs/sec')
print ("")
print ("client_exe:             " + client_exe)
print ("client_count_list:      " + ','.join (map (str, args.client_count_list)))
print ("client_cpu_list:        " + ','.join (map (str, args.client_cpu_list)))
print ("client_stats:           " + ','.join (args.client_stats))
if args.client_directory != None:
    print ("client_directory:       " + str (args.client_directory))

# Disable automatic garbage collection for performance reasons
gc.disable ()

# Create the server command
for server_rate in args.server_rate_list:
    for client_count_str in args.client_count_list:

        client_count = int (client_count_str)

        for server_queue_size in args.server_queue_size_list:

            client_cpu_list = utils.cpu_bind_list (args.client_cpu_list, client_count)

            directory = None

            if args.client_directory != None:
                directory = utils.output_directory_path (args.client_directory,
                                                    server_queue_size,
                                                    server_rate,
                                                    args.server_message_size,
                                                    client_count)
                if directory.exists () == True:
                    print ('WARNING: Ignore test. Output directory ' +
                           'already exists: ' + str (directory))
                    print ("")
                    continue

                print ("")
            # Delete shared memory if it exists
            print ('remove shared memory:' + str (args.memory_name))

            subprocess.check_call ([pathlib.Path (exe_dir) / "remove_shared_memory",
                                    "--names", args.memory_name])
            server_cmd = [server_exe,
                "--cpu",          str (args.server_cpu),
                "--name",         args.memory_name,
                "--message_size", str (args.server_message_size),
                "--queue_size",   str (server_queue_size),
                "--rate",         str (server_rate),
                "--log_level",    args.log_level]

            if args.tool_type == 'spsc':
                server_cmd += ['--clients', str (client_count)]

            # Run the server
            print ('%s' % ' '.join (map (str, server_cmd)))

            server = subprocess.Popen (server_cmd, stdout=subprocess.PIPE)

            # Allow the server time to start, then check if it is running
            time.sleep (1)
            poll = server.poll ()
            if poll != None:
                print ("spmc_server did not run")
                server.send_signal (signal.SIGINT)
                exit (1)

            # Run the client(s)
            clients = []

            stats = ','.join (args.client_stats)

            gc.collect ()
            time.sleep (int (2))

            for client_index in range (client_count):

                client_cmd = [client_exe,
                            "--name",     args.memory_name,
                            "--log_level", "INFO"]

                cpu_bind = -1
                if client_index < len (client_cpu_list):
                    cpu_bind = client_cpu_list[client_index]
                if cpu_bind != -1:
                    client_cmd.extend (["--cpu", str (cpu_bind)])

                client_cmd.extend (["--stats", stats])

                if (client_index == (client_count -1)):
                    if (directory):
                        client_cmd.extend (["--directory", directory])

                    print ('%s' % ' '.join (map (str, client_cmd)))
                    clients.append (subprocess.Popen (client_cmd))
                else:
                    print ('%s' % ' '.join (map (str, client_cmd)))
                    clients.append (subprocess.Popen (client_cmd,
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL))

            time.sleep (int (args.timeout))

            for client in clients:
                client.send_signal (signal.SIGINT)
                client.wait ()

            print ("client(s) exited")

            server.send_signal (signal.SIGINT)
            server.wait ()
            print ("server exited")

            subprocess.check_call ([os.path.join (exe_dir, "remove_shared_memory"),
                                    "--names", args.memory_name])

            print ("Performance testing finished")