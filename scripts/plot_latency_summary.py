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

import plot_utils as utils

import psutil

plt.style.use ('seaborn-darkgrid')

base_dir = Path (__file__).parent.parent.absolute ()

exe_dir = os.path.join (base_dir, 'build', platform.processor (), 'bin')

parser = argparse.ArgumentParser (description='Plot performance test results')

parser.add_argument ('--title', required=False,
                     default='Shared Memory IPC Performance',
                     help='Title of the plot')

parser.add_argument ('--server_queue_sizes', nargs='+',
                    help='Queue sizes (bytes)')
parser.add_argument ('--server_message_sizes', nargs='+',
                    help='Message size (bytes)')
parser.add_argument ('--server_rates', default='max', nargs='+',
                    help='Target messages per second. Use \'max\' '
                    '(or \'0\') for maximum throughput')

parser.add_argument ('--client_directories', nargs='+',
                    help='Base directories under which the stats data is stored')
parser.add_argument ('--client_directory_descriptions', nargs='+',
                    help='Give each directory a legend name. These should be in'
                    ' the same order as client_directories')
parser.add_argument ('--client_counts', nargs='+', type=int,
                    help='Number of consumer clients')

parser.add_argument ('--latency_log_scale', action='store_true',
                    help='Plot latency on a log scale')

parser.add_argument ('--show_throughput', action='store_true',
                    help='Plot throughput')

args = parser.parse_args ()

if not args.client_directories:
  print ('client_directories argument is not set')
  exit (1)

if args.client_directory_descriptions is not None and \
   len (args.client_directory_descriptions) != len (args.client_directories):
  print ('client_directory_descriptions count should equal client_directories count')
  exit (1)

max = '0'

print ('[INFO] server_queue_sizes:    ' + str (args.server_queue_sizes) + ' bytes')
print ('[INFO] server_message_sizes: ' + str (args.server_message_sizes) + ' bytes')
print ('[INFO] server_rates:          ' + str (args.server_rates) + ' msgs/second')
print ('[INFO] client_counts:         ' + str (args.client_counts) +
                        (' client' if args.client_counts == 1 else ' clients'))
print ('[INFO] Machine')
print ('[INFO] core_count: ' + str (psutil.cpu_count (logical=False)))
print ('[INFO] virtual_memory: ' + str (psutil.virtual_memory ()))

print ('[INFO] ' + platform.platform (terse=1))

def latency_dataframe (file_path):
  print (str (file_path))

  if file_path.exists () == False:
      print ('[ERROR] Invalid path: ' + str (file_path))
      exit (1)

  return pd.read_csv (file_path).transpose ()

def get_legend_list (data):
  legend_list = []

  if len (data['legend_texts']) > 1:
    for s in data['legend_texts']:
      legend_list.append (' '.join (s))

  return legend_list

def show_legend (data):
  if len (data) > 1:
    return True

  return False

def plot_summary_latencies (axis, latency_log_scale):

  latency_data = utils.get_latency_summary_data (args)

  axis = sns.lineplot (ax=axis, data=latency_data['latency_summaries'],
                       dashes=False)

  if latency_log_scale is True:
    axis.set (yscale='log')

  utils.set_tick_sizes (axis)

  axis.legend (get_legend_list (latency_data), fontsize=8)

  axis.set_title (' '.join (latency_data['title_texts']), fontsize=8)

  axis.set_xlabel ('Percentiles', fontsize=8)
  axis.set_ylabel ('Latency (nanoseconds)')

def plot_interval_throughput (axis, y_label, ax=None):
  print ('[INFO] Plot: ' + y_label)

  throughput_data = utils.get_throughput_interval_data (args)
  interval_data   = throughput_data['throughput_intervals']

  plt = sns.lineplot (ax=axis, data=interval_data['dataframe'][y_label],
                      dashes=False)

  if axis.get_legend () is not None:
    axis.get_legend ().remove ()

  utils.set_tick_sizes (axis)

  axis.set_xlabel ('Time (secs)\n\n'
                  + platform.platform (terse=1) + '\n'
                  + utils.get_hardware_stats (), fontsize=8)

  return plt

#
# Plot latency percentiles
#
# Throughput over time is plotted if explicitly enabled.
#
if args.show_throughput is True:

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
  plot_interval_throughput (lhs_axis, 'messages_per_sec')

  # Plot throughput bytes/sec on the other y-axis
  rhs_axis.set_ylabel ('bytes/sec', fontsize='9')
  plot_interval_throughput (rhs_axis, 'bytes_per_sec')
  rhs_axis.tick_params (axis='y', grid_alpha=0)


else:
  plot_summary_latencies (axis=None, latency_log_scale=args.latency_log_scale)

plt.suptitle (args.title, fontsize=10)

plt.show ()
