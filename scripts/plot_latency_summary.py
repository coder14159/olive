#!/usr/bin/env python3

##########################################
# Script to plot latency profile summary #
##########################################

import argparse
import cpuinfo
import matplotlib.pyplot as plt
import numpy as np
import os
import pandas as pd
import platform
import seaborn as sns

from pathlib import Path

import plot_utils as utils

plt.style.use ('seaborn-darkgrid')

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + '/../')
exe_dir = os.path.join (base_dir, 'build', platform.processor (), 'bin')

parser = argparse.ArgumentParser (description='Plot performance test results')

parser.add_argument ('--title', required=False,
                     default='IPC Performance Profile',
                     help='Title of the plot')
parser.add_argument ('--subtitle', required=False,  help='Subtitle of the plot')

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

print ('server_queue_size:    ' + str (args.server_queue_sizes) + ' bytes')
print ('server_message_sizes:  ' + str (args.server_message_sizes) + ' bytes')
print ('server_rate:          ' + str (args.server_rates) + ' messages/second')
print ('client_count:         ' + str (args.client_counts) +
                        (' client' if args.client_counts == 1 else ' clients'))

def sub_title (server_rate, message_size, client_count):
  # A value of zero also denotes max server rate
  server_rate = (server_rate if server_rate != 'max' else max)

  return 'message_rate='  + utils.throughput_messages_to_pretty (server_rate) \
       + ' message_size=' + str (message_size) + ' bytes' \
       + ' client_count=' + str (client_count)

def latency_dataframe (file_path):

  print (str (file_path))

  if file_path.exists () == False:
      print ('Path does not exist: ' + str (file_path))
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

def set_tick_sizes (axis):
  for tick in axis.xaxis.get_major_ticks ():
      tick.label.set_fontsize (8)
  for tick in axis.yaxis.get_major_ticks ():
      tick.label.set_fontsize (8)

def plot_summary_latencies (axis, latency_log_scale):

  latency_data = utils.get_latency_summary_data (args)

  legend = show_legend (latency_data['latency_summaries'])

  axis = sns.lineplot (ax=axis, data=latency_data['latency_summaries'],
                       dashes=False, legend=legend)

  if latency_log_scale is True:
    axis.set (yscale='log')

  set_tick_sizes (axis)

  axis.legend (get_legend_list (latency_data), fontsize=8)

  axis.set_title (' '.join (latency_data['title_texts']), fontsize=10)
  axis.set_xlabel ('Percentiles', fontsize=9)
  axis.set_ylabel ('Latency (nanoseconds)', fontsize=9)

def plot_interval_throughput (axis):
  throughput_data = utils.get_throughput_interval_data (args)

  axis = sns.lineplot (ax=axis, data=throughput_data['throughput_intervals'],
                       dashes=False)

  axis.legend (get_legend_list (throughput_data), fontsize=8)

  axis.set_xlabel ('Time (seconds)', fontsize=9)
  axis.set_ylabel ('Throughput (MB/sec)', fontsize=9)

  return axis

#
# Plot latency percentiles
#
# Throughput over time is plotted if explcitly enabled.
# This is useful at high throughput values
#
if args.show_throughput is True:
  fig, axes = plt.subplots (2, 1, gridspec_kw={'height_ratios': [3, 1]})

  plot_summary_latencies (axes[0], args.latency_log_scale)

  plot_interval_throughput (axes[1])

else:
  plot_summary_latencies (axis=None, latency_log_scale=args.latency_log_scale)

plt.suptitle (args.title)

plt.show ()
