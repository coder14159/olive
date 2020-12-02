#!/usr/bin/env python3

import argparse
import os
import pandas as pd
import pathlib
import platform
import plotly.express as px
import socket

from test_utils import *

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + "/../")
exe_dir = os.path.join (base_dir, "build", platform.processor (), "bin")

parser = argparse.ArgumentParser (description="Plot performance test results")

# parser.add_argument ("--description",   help="description of plot")
parser.add_argument ("--server_queue_size",   required=True,
                    help="queue size (bytes)")
parser.add_argument ("--server_message_size", help="message size (bytes)")
parser.add_argument ("--server_rate",         help="messages per second")

parser.add_argument ("--client_stats", nargs='+', default="",
                    choices=["interval", "latency", "throughput"],
                    help="select statistics for output")
parser.add_argument ("--client_directories", nargs='+',
                    help="directories to read stats data")

args = parser.parse_args ()

if not args.directories:
    print ("directories argument not set")
    exit (1)

print ("directories: " + str (args.directories))

print ("server_queue_size:   " + str (args.server_queue_size) + " bytes")
print ("server_message_size: " + str (args.server_message_size) + " bytes")
print ("server_rate:         " + str (args.server_rate) + " messages/second")
print ("")

for directory in args.directories:
   path = pathlib.Path (directory)                          \
        / "server_rate" / str (args.server_rate)            \
        / "client_count" / str (args.client_count)          \
        / "message_size" / str (args.server_message_size)   \
        /

    data = pd.read_csv ()

directory = directory_path (directory)



