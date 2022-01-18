#!/usr/bin/env python3

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

import psutil

plt.style.use ('seaborn-darkgrid')

script_dir = os.path.dirname (os.path.realpath(__file__))

base_dir = os.path.normpath (script_dir + "/../")
exe_dir = os.path.join (base_dir, "build", platform.processor (), "bin")

parser = argparse.ArgumentParser (description="Plot performance test results")

parser.add_argument ("--title", required=False,
                     default='Shared Memory IPC Performance',
                     help="Title of the plot")
parser.add_argument ("--subtitle", required=False,  help="Subtitle of the plot")

parser.add_argument ("--client_directories", nargs='+',
                    help="Base directories under which the stats data is stored")
parser.add_argument ("--version", required=False, default='1', help="Output data version")

parser.add_argument ("--server_queue_sizes", nargs='+', help="Queue size (bytes)")
parser.add_argument ("--server_message_sizes", nargs='+', help="Message size (bytes)")
parser.add_argument ("--server_rates", default="max", nargs='+',
                    help="Target messages per second.Value 'max' is maximum throughput \
                    (default is maximum)")

parser.add_argument ("--client_stats", nargs='+',
                    choices=["interval", "latency", "throughput"],
                    help="Select statistics for output")

latency_list=['0', '1', '5', '25', '50', '75', '80', '90', '95', '99', '99.5',
              '99.6', '99.7', '99.8', '99.9', '99.95', '99.99', '100']
parser.add_argument ("--client_latency_percentiles", nargs='+',
                    choices=latency_list, default=latency_list,
                    help="Select latency percentiles to display")
parser.add_argument ("--client_count",  type=int, required=True,
                    help="Number of consumer clients")

args = parser.parse_args ()

if not args.client_directories:
  print ("client directories argument is not set")
  exit (1)

print ('[INFO] server_queue_sizes:    ' + str (args.server_queue_sizes) + ' bytes')
print ('[INFO] server_message_sizes:  ' + str (args.server_message_sizes) + ' bytes')
print ('[INFO] server_rates:          ' + str (args.server_rates) + ' msgs/second')
print ('[INFO] client_count:          ' + str (args.client_count))
print ('[INFO] Machine')
print ('[INFO] core_count: ' + str (psutil.cpu_count (logical=False)))
print ('[INFO] virtual_memory: ' + str (psutil.virtual_memory ()))

def plot_latencies (file_path, title):

  print (str (file_path))

  if file_path.exists () == False:
      print ("Path does not exist: " + str (file_path))
      exit (1)

  df = pd.read_csv (file_path)

  percentiles = sorted (np.array (args.client_latency_percentiles),
                           key=float, reverse=True)

  plt_axis = plt.subplot2grid ((10,10), (0,0), rowspan=9, colspan=10)

  sns.lineplot (ax=plt_axis, data=df[percentiles], dashes=False)

  utils.set_tick_sizes (plt_axis)

  return plt_axis

plot = None
data_frame = None
for dir in args.client_directories:

  for server_rate in args.server_rates:
    for server_queue_size in args.server_queue_sizes:

      if Path (dir).exists () == False:
          print ('path does not exist: ' + dir)
          exit (1)

      for message_size in args.server_message_sizes:
        # print ("args.client_prefetch_size: " + args.client_prefetch_size)
        print ("message_size: " + message_size)
        print ("queue_size: " + server_queue_size)
        # print ("args.server_message_size: " + args.server_message_size)

        data_directory = utils.output_directory_path (dir,
                                        server_queue_size,
                                        server_rate,
                                        message_size,
                                        args.client_count)
        file_path = Path (data_directory) / 'latency-interval.csv'

        if file_path.exists () == False:
            print ("Path does not exist: " + str (file_path))
            exit (1)

        plot = plot_latencies (file_path, "title")

        # if data_frame is None:
        #   data_frame = pd.read_csv (file_path)
        # else:
        #   data_frame.append (pd.read_csv (file_path))

  # percentiles = sorted (np.array (args.client_latency_percentiles),
  #                   key=float, reverse=True)
  # print (data_frame)
  # plot = sns.lineplot (data=data_frame[percentiles], dashes=False)


plt.suptitle (args.title)
plt.title ('Latency Percentiles', fontsize=10)

plot.set_xlabel ('Time (secs)\n\n'
                + platform.platform (terse=1) + '\n'
                + utils.get_hardware_stats (), fontsize=8)
# plot.set (xlabel ='Time (seconds)', ylabel='Latency (nanoseconds)')
# plt.title (title, fontsize=8)

plt.legend (bbox_to_anchor=(1, 1), loc='upper left',
            title='Percentiles', title_fontsize='small',
            fontsize='small')
# plt.title ('Latency Percentiles', prop={'size':'small'})

plt.show ()
