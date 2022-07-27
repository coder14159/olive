#!/usr/bin/env python3

import argparse
import cpuinfo
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import numpy as np
import os
import pandas as pd
import platform
import queue
import seaborn as sns

from pathlib import Path

import logging
from logging import DEBUG, INFO, WARNING, ERROR, CRITICAL

import plot_utils as utils

import psutil

plt.style.use ("seaborn-darkgrid")

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + "/../")
exe_dir = os.path.join (base_dir, "build", platform.processor (), "bin")

parser = argparse.ArgumentParser (description="Plot performance test results")

parser.add_argument ("--title", required=False,
                     default="Shared Memory IPC Interval Profile",
                     help="Title of the plot")
# parser.add_argument ("--subtitle", default="Latency Interval Profile",
#                     required=False,  help="Subtitle of the plot")
parser.add_argument ("--version", required=False, default="1.0",
                    help="Output data version")

parser.add_argument ("--server_queue_sizes", nargs="+", required=True,
                    help="Queue size (bytes)")
parser.add_argument ("--server_message_sizes", nargs="+", required=True,
                    help="Message size (bytes)")
parser.add_argument ("--server_rates", default="max", nargs="+",
                    help="Target messages per second.Value 'max' is maximum "
                    "throughput (default is maximum)")

parser.add_argument ("--client_directories", nargs="+", required=True,
                    help="Base directories under which the stats data is stored")
parser.add_argument ("--client_directory_descriptions", nargs="+", required=True,
                    help="Give each directory a legend name. These should be in"
                    " the same order as client_directories")
parser.add_argument ("--client_counts", nargs="+", required=True,
                    help="Number of consumer clients")

latency_list=["0", "1", "10", "25", "50", "75", "80", "90", "95", "99", "99.5",
              "99.6", "99.7", "99.8", "99.9", "99.95", "99.99", "100"]
parser.add_argument ("--client_latency_percentiles", nargs="+",
                    choices=latency_list, default=["99"],
                    help="Select latency percentiles to display")

parser.add_argument ("--log_level", required=False, default="INFO",
                    choices=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
                    help="Logging level")

args = parser.parse_args ()

if len (args.client_directory_descriptions) != len (args.client_directories):
  print ('client_directory_descriptions count should equal client_directories count')
  exit (1)

logger = utils.init_logger (logging, args.log_level)

utils.log_machine_specs (logger)

utils.log_run_args (logger, args)

logger.info ("percentiles:          " +
            utils.join_list (args.client_latency_percentiles))


def plot_interval_latencies (axis):

  latency_data = utils.get_latency_interval_data (args)

  plt.suptitle (args.title, fontsize=10)

  interval_data = latency_data["latency_intervals"]

  axis = sns.lineplot (ax=axis, data=interval_data["latencies"], dashes=False)

  utils.set_tick_sizes (axis)

  # Position the legend outside of the plot
  axis.legend (bbox_to_anchor=(1.02, 1), loc="upper left",
               borderaxespad=0, fontsize=8)

  axis.legend (utils.get_legend_list (latency_data), fontsize=8)

  axis.set_title (" ".join (latency_data["title_texts"]), fontsize=8)

  axis.set_xlabel ("Time (secs)\n\n"
                  + platform.platform (terse=1) + "\n"
                  + utils.get_hardware_specs (), fontsize=8)
  axis.set_ylabel ("Latency (nanoseconds)", fontsize=8)

axis = plt.subplot2grid ((10,9), (0,0), rowspan=9, colspan=10)

logger.info ("Plotting...")

plot_interval_latencies (axis)

plt.suptitle (args.title, fontsize=10)

plt.show ()

logger.info ("Exit")
