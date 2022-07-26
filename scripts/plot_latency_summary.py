#!/usr/bin/env python3

##########################################
# Script to plot latency profile summary #
##########################################

import argparse
import matplotlib.gridspec as gridspec
import matplotlib.pyplot as plt
import os
import pandas as pd
import platform
import seaborn as sns

from pathlib import Path

import logging
from logging import DEBUG, INFO, WARNING, ERROR, CRITICAL

import plot_utils as utils

import psutil

plt.style.use ('seaborn-darkgrid')

base_dir = Path (__file__).parent.parent.absolute ()

exe_dir = os.path.join (base_dir, 'build', platform.processor (), 'bin')

parser = argparse.ArgumentParser (description='Plot performance test results')

parser.add_argument ('--title', required=False,
                     default='Shared Memory IPC Performance Summary',
                     help='Title of the plot')

parser.add_argument ('--server_queue_sizes', nargs='+', required=True,
                    help='Queue sizes (bytes)')
parser.add_argument ('--server_message_sizes', nargs='+', required=True,
                    help='Message size (bytes)')
parser.add_argument ('--server_rates', default='max', nargs='+', required=True,
                    help='Target messages per second. Use \'max\' '
                    '(or \'0\') for maximum throughput')

parser.add_argument ('--client_directories', nargs='+', required=True,
                    help='Base directories under which the stats data is stored')
parser.add_argument ('--client_directory_descriptions', nargs='+', required=True,
                    help='Give each directory a legend name. These should be in'
                    ' the same order as client_directories')
parser.add_argument ('--client_counts', nargs='+', required=True,
                    help='Number of consumer clients')

parser.add_argument ('--latency_log_scale', action='store_true',
                    help='Plot latency on a log scale')

parser.add_argument ('--show_throughput', default=False, action='store_true',
                    help='Plot throughput')

parser.add_argument ("--log_level", required=False, default="INFO",
                    choices=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
                    help="Logging level")


args = parser.parse_args ()

if len (args.client_directory_descriptions) != len (args.client_directories):
  print ('client_directory_descriptions count should equal client_directories count')
  exit (1)

# Ignore font logging
class LocalLogger(logging.Logger):

  def __init__(self, name):
    logging.Logger.__init__(self, name)
    self.addFilter(self.MyFilter())

  class MyFilter(logging.Filter):
    def filter(self, record):
      if record.name != 'findfont':
        return False
      return True

logging.setLoggerClass(LocalLogger)

max = '0'

logger = utils.init_logger (logging, args.log_level)

utils.log_machine_specs (logger)

utils.log_run_args (logger, args)

def show_legend (data):
  if len (data) > 1:
    return True

  return False

def plot_summary_latencies (axis, latency_log_scale, show_platform=False):

  latency_data = utils.get_latency_summary_data (args)

  axis = sns.lineplot (ax=axis, data=latency_data['latency_summaries'],
                       dashes=False)

  if latency_log_scale == True:
    axis.set (yscale='log')

  utils.set_tick_sizes (axis)

  axis.legend (utils.get_legend_list (latency_data), fontsize=8)

  axis.set_title (' '.join (latency_data['title_texts']), fontsize=8)

  xlabel = 'Percentiles'

  if show_platform == True:
    xlabel += '\n\n' + platform.platform (terse=1) + '\n' \
                     + utils.get_hardware_specs ()

  axis.set_xlabel (xlabel, fontsize=8)
  axis.set_ylabel ('Latency (nanoseconds)')

def plot_interval_throughput (axis, y_label, show_platform=False):

  throughput_data = utils.get_throughput_interval_data (args)
  interval_data   = throughput_data['throughput_intervals']

  plt = sns.lineplot (ax=axis, data=interval_data['dataframe'][y_label],
                      dashes=False)

  if axis.get_legend () != None:
    axis.get_legend ().remove ()

  utils.set_tick_sizes (axis)

  xlabel = 'Time (secs)'

  if show_platform == True:
    xlabel += '\n\n' + platform.platform (terse=1) + '\n' + utils.get_hardware_specs ()

  axis.set_xlabel (xlabel, fontsize=8)

  return plt

#
# Plot latency percentiles
#
# Throughput over time is plotted if explicitly enabled.
#
if args.show_throughput == True:

  fig = plt.figure ()

  # Prepare latency distribution graph
  plt_latency = plt.subplot2grid ((10,10), (0,0), colspan=10, rowspan=5)
  plt_latency.set_xlabel ('Percentiles', fontsize='9')
  plt_latency.set_ylabel ('Latency (nanoseconds)', fontsize='9')

  # Plot latency distributions
  plot_summary_latencies (plt_latency, latency_log_scale=args.latency_log_scale)

  # Plot throughput messages/sec
  lhs_axis = plt.subplot2grid ((10,10), (6,0), colspan=10, rowspan=3)
  rhs_axis = lhs_axis.twinx ()

  lhs_axis.set_ylabel ('messages/sec', fontsize='9')
  plot_interval_throughput (lhs_axis, 'messages_per_sec', show_platform=True)

  # Plot throughput bytes/sec on the other y-axis
  rhs_axis.set_ylabel ('bytes/sec', fontsize='9')
  plot_interval_throughput (rhs_axis, 'bytes_per_sec')
  rhs_axis.tick_params (axis='y', grid_alpha=0)

else:
  plt_latency = plt.subplot2grid ((10,10), (0,0), colspan=10, rowspan=9)

  plot_summary_latencies (plt_latency, latency_log_scale=args.latency_log_scale,
                          show_platform=True)

plt.suptitle (args.title, fontsize=10)

plt.show ()
