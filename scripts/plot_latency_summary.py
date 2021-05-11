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
import queue
import seaborn as sns

from pathlib import Path

import performance_utils as utils

plt.style.use ('seaborn-darkgrid')

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + '/../')
exe_dir = os.path.join (base_dir, 'build', platform.processor (), 'bin')

parser = argparse.ArgumentParser (description='Plot performance test results')

parser.add_argument ('--title', required=False,
                     default='Latency Percentile Distribution',
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
parser.add_argument ('--client_prefetch_sizes', required=False, default='0',
                    nargs='+', help='Client prefetch size')

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
print ('client_count:         ' + str (args.client_counts)
                                + (' client' if args.client_counts == 1
                              else ' clients'))
print ('client_prefetch_sizes: ' + str (args.client_prefetch_sizes))

def sub_title (server_rate, message_size, client_count):
  # An integer of zero denotes maximum server rate
  server_rate = (server_rate if server_rate != 'max' else '0')

  return 'message_rate='  + utils.throughput_messages_to_pretty (server_rate) \
       + ' message_size=' + str (message_size) + ' bytes' \
       + ' client_count=' + str (client_count)

def latency_dataframe (file_path):

  print (str (file_path))

  if file_path.exists () == False:
      print ('Path does not exist: ' + str (file_path))
      exit (1)

  return pd.read_csv (file_path).transpose ()

def plot_latency_summary (axis, file_path, title, legend):

  df = latency_dataframe (file_path)

  axis = sns.lineplot (data=df, dashes=False)\
            .set (xlabel='percentile', ylabel='latency (nanoseconds)')

  plt.legend ().get_texts ()[index].set_text (legend)

  return axis

index = 1
axis = None

def get_plot_data ():
  legend_texts = []
  legend_prefix_texts = None
  title_text = set ()
  dataframe = None
  frame_count = 0

  if args.client_directory_descriptions is not None:
    legend_prefix_texts = queue.Queue ()

    for prefix in args.client_directory_descriptions:
      legend_prefix_texts.put (prefix)

  for dir in args.client_directories:

    legend_prefix = ''
    if legend_prefix_texts is not None:
      legend_prefix = legend_prefix_texts.get ()

    for server_rate in args.server_rates:
      for server_queue_size in args.server_queue_sizes:

        if Path (dir).exists () == False:
            print ('path does not exist: ' + dir)
            continue

        for message_size in args.server_message_sizes:

          for client_count in args.client_counts:

            for client_prefetch_size in args.client_prefetch_sizes:

              data_directory = utils.output_directory_path (dir,
                                              server_queue_size,
                                              server_rate,
                                              message_size,
                                              client_count,
                                              client_prefetch_size)

              file_path = Path (data_directory) / 'latency-summary.csv'

              if file_path.exists () == False:
                print (str (file_path) + " does not exist")
                continue

              print ('loading: ' + str (file_path))
              df = pd.read_csv (file_path).transpose ()

              if dataframe is None:
                dataframe = df
              else:
                frame_count += 1
                dataframe[frame_count] = df

              legend_line_label = [legend_prefix + ' ']

              rate = (server_rate if server_rate != '0' else 'max')

              if len (args.server_rates) > 1:
                legend_line_label.append ('rate:' + str (rate))
              else:
                title_text.add ('rate:' + str (rate))

              if len (args.server_message_sizes) > 1:
                legend_line_label.append ('message_size:' + str (message_size))
              else:
                title_text.add ('msg_size:' + str (message_size))

              if len (args.server_queue_sizes) > 1:
                legend_line_label.append ('queue_size:' + str (server_queue_size))
              else:
                title_text.add ('queue_size:' + str (server_queue_size))

              if len (args.client_counts) > 1:
                legend_line_label.append ('clients:' + str (client_count))
              else:
                title_text.add ('clients:' + str (client_count))

              if len (args.client_prefetch_sizes) > 1:
                legend_line_label.append ('prefetch_size:' + str (client_prefetch_size))
              else:
                title_text.add ('prefetch_size:' + str (client_prefetch_size))

              legend_texts.append (legend_line_label)

  return dict (latency_summaries=dataframe,
               legend_texts=legend_texts,
               title_texts=title_text)

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

data = get_plot_data ()

legend_list = get_legend_list (data)

show_legend = show_legend (data['latency_summaries'])

axis = sns.lineplot (data=data['latency_summaries'], dashes=False,
                     legend=show_legend)
set_tick_sizes (axis)

axis.legend (get_legend_list (data), fontsize=8)

axis.set_title (' '.join (data['title_texts']), fontsize=10)
axis.set_xlabel ('Percentile', fontsize=10)
axis.set_ylabel ('Latency (nanoseconds)', fontsize=10)

plt.suptitle (args.title)
plt.show ()
