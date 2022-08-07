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
base_dir   = os.path.normpath (script_dir + "/../")
exe_dir    = os.path.join (base_dir, "build", platform.processor (), "bin")

parser = argparse.ArgumentParser (description="Plot performance test results")

parser.add_argument ("--title", required=False,
                     default="Shared Memory IPC Interval Profile",
                     help="Title of the plot")
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

parser.add_argument ('--show_throughput', default=False, action='store_true',
                    help='Plot throughput')

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

def plot_interval_latencies (axis, show_platform=False):

  latency_data = utils.get_latency_interval_data (args)

  plt.suptitle (args.title, fontsize=10)

  interval_data = latency_data["latency_intervals"]

  axis = sns.lineplot (ax=axis, data=interval_data["latencies"], dashes=False)

  utils.set_tick_sizes (axis)

  axis.set_title (" ".join (latency_data["title_texts"]), fontsize=8)

  axis.set_ylabel ("Latency (nanoseconds)")

  plt.suptitle (args.title, fontsize=10)

  utils.set_tick_sizes (axis)

  axis.legend (utils.get_legend_list (latency_data), fontsize=8)

  axis.set_title (" ".join (latency_data["title_texts"]), fontsize=8)

  if show_platform == True:
    axis.set_xlabel ("Time (secs)\n\n"
                    + platform.platform (terse=1) + "\n"
                    + utils.get_hardware_specs (), fontsize=8)

  axis.set_ylabel ("Latency (nanoseconds)", fontsize=8)
################################################################################

logger.info ("=====Plotting=========")

#
# Plot latency percentiles
#
# Throughput over time is plotted if explicitly enabled.
#
if args.show_throughput == True:

  fig = plt.figure ()

  # Prepare interval latency graph
  plt_latency = plt.subplot2grid ((10,10), (0,0), colspan=10, rowspan=5)
  plt_latency.set_ylabel ('Latency (nanoseconds)', fontsize='9')

  # Plot latency distributions
  plot_interval_latencies (plt_latency)

  # Plot throughput messages/sec
  lhs_axis = plt.subplot2grid ((10,10), (6,0), colspan=10, rowspan=3)
  rhs_axis = lhs_axis.twinx ()

  lhs_axis.set_ylabel ('messages/sec', fontsize='9')

  throughput_interval_data = utils.get_throughput_interval_data (args)

  utils.plot_interval_throughput (throughput_interval_data,
                        lhs_axis, 'messages_per_sec', show_platform=True)

  # Plot throughput bytes/sec on the other y-axis
  rhs_axis.set_ylabel ('bytes/sec', fontsize='9')

  utils.plot_interval_throughput (throughput_interval_data,
                        rhs_axis, 'bytes_per_sec')

  rhs_axis.tick_params (axis='y', grid_alpha=0)

else:
  axis = plt.subplot2grid ((10,9), (0,0), rowspan=9, colspan=10)

  plot_interval_latencies (axis, show_platform=True)

plt.suptitle (args.title, fontsize=10)

plt.show ()
