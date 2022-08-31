#!/usr/bin/env python3

import os
from pathlib import Path
import pandas as pd
import numpy as np
import platform
import psutil
import queue
import seaborn as sns

import logging
from logging import DEBUG, INFO, WARNING, ERROR, CRITICAL

log_levels = {
    'critical': CRITICAL,
    'error':    ERROR,
    'warn':     WARNING,
    'warning':  WARNING,
    'info':     INFO,
    'debug':    DEBUG
}

# This statement stops the logger outputting the notification below
# "QSocketNotifier: Can only be used with threads started with QThread"
os.environ["XDG_SESSION_TYPE"] = "x11"

#
# Initialise the logger
#
def init_logger (logger, level_str):
  logging.basicConfig (level=log_levels.get (level_str.lower ()),
    format='%(asctime)s %(levelname)8s: %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S')

  return logging

#
# Maybe use this function for more human-friendly plotting values
#
# def sub_title (server_rate, message_size, client_count):
#   # An integer of zero denotes maximum server rate
#   server_rate = (server_rate if server_rate != 'max' else '0')

#   return 'message_rate='  + throughput_messages_to_pretty (server_rate) \
#        + ' message_size=' + str (message_size) + ' bytes' \
#        + ' client_count=' + str (client_count)

def set_tick_sizes (axis):
  for tick in axis.xaxis.get_major_ticks ():
      tick.label.set_fontsize (8)
  for tick in axis.yaxis.get_major_ticks ():
      tick.label.set_fontsize (8)

  axis.tick_params (axis='y', labelsize=8)

#
# Update the CPU bind list to use -1 for clients which do not bind to a cpu
#
NO_CPU_BIND = -1

def cpu_bind_list (cpu_list, client_count):

  while len (cpu_list) < client_count:
    cpu_list.insert (0, NO_CPU_BIND)

  return cpu_list
#
# Return a directory based on the parameters supplied
#
def output_directory_path (
  base_directory,
  server_queue_size, server_rate, server_message_size,
  client_count):

  server_rate_str = 'max' if server_rate == '0' else str (server_rate)

  path = Path (base_directory) \
            / 'server_queue_size'    / str (server_queue_size) \
            / 'server_rate'          / server_rate_str  \
            / 'server_message_size'  / str (server_message_size) \
            / 'client_count'         / str (client_count)

  return path
#
# Return a human readable message throughput string
#
def throughput_messages_to_pretty (rate):
  if rate == 'max' or rate == '0':
    return 'max'

  K = 1.0e3
  M = 1.0e6
  G = 1.0e6

  float_rate = float (rate)

  if float_rate > G:
    return str (round (float_rate / G, 1)) + ' G msgs/s'
  if float_rate > M:
    return str (round (float_rate / M, 1)) + ' M msgs/s'
  if float_rate > K:
    return str (round (float_rate / K)) + ' K msgs/s'

  return str (round (float_rate)) + ' msgs/s'
#
# Return human readable data size string
#
def size_to_pretty (bytes, suffix='B'):
  factor = 1024

  for unit in ['', 'K', 'M', 'G', 'T', 'P']:
    if bytes < factor:
      return f'{bytes:.2f} {unit}{suffix}'
    bytes /= factor
#
# Return legend text
#
def get_legend_list (data):
  return data['legend_texts']

#
# Load performance data from CSV files and generate data for plotting
#
def load_performance_data (args, filename):
  legend_texts = []
  legend_prefix_texts = None
  plot_texts = dict ()
  dataframe = None

  throughputs = {}
  throughput_column_count = 0

  latencies = {}
  latency_column_count = 0

  # TODO: Prefer to use a local logger rather than filtering
  # eg mylogger = logging.getLogger('UNIQUE_NAME')
  # log.setLevel(logging.DEBUG)
  #
  # Redirect matplotlib output to workaround the logging of the line
  # "INFO: Using categorical units to plot a list of strings that are all parsable"
  #
  flogging = logging.getLogger ('matplotlib.findfont')
  flogging.setLevel (logging.WARNING)

  mlogging = logging.getLogger ('matplotlib')
  mlogging.setLevel (logging.WARNING)
  #

  if args.client_directory_descriptions != None:
    legend_prefix_texts = queue.Queue ()

    for prefix in args.client_directory_descriptions:
      legend_prefix_texts.put (prefix)

  join_legend_list = False

  for dir in args.client_directories:

    legend_prefix = ''
    if legend_prefix_texts != None:
      legend_prefix = legend_prefix_texts.get ()

    for server_rate in args.server_rates:
      for server_queue_size in args.server_queue_sizes:

        if Path (dir).exists () == False:
          logging.error ('Invalid directory: ' + dir)
          continue

        for message_size in args.server_message_sizes:

          for client_count in args.client_counts:
            join_legend_list = False

            data_directory = output_directory_path (dir,
                                            server_queue_size,
                                            server_rate,
                                            message_size,
                                            client_count)

            file_path = Path (data_directory) / filename

            if file_path.exists () == False:
              logging.error ('Invalid path: ' + str (file_path))
              continue

            logging.info ('Loading: ' + str (file_path))
            df = pd.read_csv (file_path)

            if 'latency-summary' in filename:
              join_legend_list = True

              df = df.transpose ()

              if dataframe is None:
                dataframe = df.astype (int)
              else:
                throughput_column_count += 1
                dataframe[throughput_column_count] = df.astype (int)

              legend_texts.append (legend_prefix)

            if 'throughput-interval' in filename:
              join_legend_list = True

              if not throughputs:
                throughputs['messages_per_sec'] = pd.DataFrame (
                    { throughput_column_count : df['messages_per_sec'] })
                throughputs['bytes_per_sec'] = pd.DataFrame (
                    { throughput_column_count : df['bytes_per_sec'] })

                dataframe = throughputs
              else:
                throughput_column_count += 1
                throughputs['messages_per_sec'] \
                           [throughput_column_count] = df['messages_per_sec']
                throughputs['bytes_per_sec'] \
                           [throughput_column_count] = df['bytes_per_sec']

                dataframe = throughputs

              legend_texts.append (legend_prefix)

            if 'latency-interval' in filename:
              join_legend_list = True

              if not latencies:
                latencies['latencies'] = pd.DataFrame ()

              for percentile in args.client_latency_percentiles:
                legend_line = []

                latencies['latencies'] \
                         [latency_column_count] = df[percentile].astype (int)

                latency_column_count += 1

                legend_line.append (legend_prefix + ':' + percentile + '%')

                texts = get_plot_texts (args, legend_line,
                                  message_size, server_rate, server_queue_size,
                                  client_count, join_legend_list)
                if plot_texts:
                  plot_texts['legend_texts'] += texts['legend_texts']
                else:
                  plot_texts['legend_texts'] = texts['legend_texts']

              dataframe = latencies

              plot_texts['title_texts'] = texts['title_texts']

            else:
              texts = get_plot_texts (args, legend_texts,
                              message_size, server_rate, server_queue_size,
                              client_count, join_legend_list)

              if plot_texts:
                plot_texts['legend_texts'] += texts['legend_texts']
              else:
                plot_texts['legend_texts'] = texts['legend_texts']

              plot_texts['title_texts'] = texts['title_texts']

              texts = []
              legend_texts = []

  if plot_texts == {}:
    return None
  if len (plot_texts['legend_texts']) == 1:
    plot_texts['legend_texts'] = []

  return dict (dataframe=dataframe,
               legend_texts=plot_texts['legend_texts'],
               title_texts=plot_texts['title_texts'])

#
# Get text descriptions for a plot
#
def get_plot_texts (args, legend_texts, message_size, server_rate,
                    server_queue_size, client_count,
                    join_legend_list=False):
  # Construct line descriptions
  title_text = set ()

  rate = (server_rate if server_rate != '0' else 'max')

  if len (args.client_counts) > 1:
    legend_texts.append ('clients:' + str (client_count))
  else:
    title_text.add ('clients:' + str (client_count))

  if len (args.server_message_sizes) > 1:
    legend_texts.append ('message_size:' + str (message_size))
  else:
    title_text.add ('message_size:' + str (message_size))

  if len (args.server_queue_sizes) > 1:
    legend_texts.append ('queue_size:' + str (server_queue_size))
  else:
    title_text.add ('queue_size:' + str (server_queue_size))

  if len (args.server_rates) > 1:
    legend_texts.append ('rate:' + str (rate))
  else:
    title_text.add ('rate:' + str (rate))

  if join_legend_list == True:
    legend_texts = [join_list (legend_texts, ' ')]

  return dict (legend_texts=legend_texts,
               title_texts=title_text)

#
# Get latency summary data
#
def get_latency_summary_data (args):

  latency_data = load_performance_data (args,'latency-summary.csv')

  if latency_data == None:
    exit (1)

  return dict (latency_summaries=latency_data['dataframe'],
               legend_texts=latency_data['legend_texts'],
               title_texts=latency_data['title_texts'])
#
# Get latency data for a plot
#
def get_latency_interval_data (args):

  latency_data = load_performance_data (args,'latency-interval.csv')

  if latency_data == None:
    exit (1)

  return dict (latency_intervals=latency_data['dataframe'],
               legend_texts=latency_data['legend_texts'],
               title_texts=latency_data['title_texts'])
#
# Get interval throughput data for a plot
#
def get_throughput_interval_data (args):

  throughput_data = load_performance_data (args, 'throughput-interval.csv')

  return dict (throughput_intervals=throughput_data,
               legend_texts=throughput_data['legend_texts'],
               title_texts=throughput_data['title_texts'])
#
# Get interval throughput data for a plot
#
def plot_interval_throughput (throughput_data, axis, y_label,
                              show_platform=False, show_legend=True):

  interval_data = throughput_data['throughput_intervals']

  plt = sns.lineplot (ax=axis, data=interval_data['dataframe'][y_label],
                      dashes=False)

  axis.legend (get_legend_list (throughput_data), fontsize=8)

  if show_legend == False:
    axis.get_legend ().set_visible (False)

  set_tick_sizes (axis)

  xlabel = 'Time (secs)'

  if show_platform == True:
    xlabel += '\n\n' + platform.platform (terse=1)  \
            + '\n' + get_hardware_specs ()

  axis.set_xlabel (xlabel, fontsize=8)

  return plt

#
# Print local machine specs
#
def log_machine_specs (logger):
  vm = psutil.virtual_memory ()

  logger.info ("=====Host Machine=====")
  logger.info ("cpu_count:            " + str (psutil.cpu_count (logical=False)))
  logger.info ("cpu_frequency_max     " + str (int (psutil.cpu_freq ().max)) + " MHz")
  logger.info ("memory_total:         " + str (size_to_pretty (vm.total)))
  logger.info ("memory_available:     " + str (size_to_pretty (vm.available)))

#
# Convert a list to a comma separated (by default) string
#
def join_list (list, sep=','):
  return str (sep.join (list))

#
# Print arguments of a plot
#
def log_run_args (logger, args):

  logger.info ("=====Parameters=======")
  logger.info ("server_queue_sizes:   " + join_list (args.server_queue_sizes) + " bytes")
  logger.info ("server_message_sizes: " + join_list (args.server_message_sizes) + " bytes")
  logger.info ("server_rates:         " + join_list (args.server_rates) + " msgs/second")
  logger.info ("client_counts:        " + join_list (args.client_counts))

#
# Print specs for the current machine
#
def get_hardware_specs ():

  stats = f'Processor core_count={psutil.cpu_count (logical=False)} '
  stats += f'core_frequency min={psutil.cpu_freq ().min:.0f}Mhz '
  stats += f'max={psutil.cpu_freq ().max:.0f}Mhz'

  svmem = psutil.virtual_memory ()
  stats += f'\nRAM total={size_to_pretty (svmem.total)}'
  stats += f' free={size_to_pretty (svmem.available)}'
  stats += f' used={size_to_pretty (svmem.used)} ({svmem.percent}%)'

  return stats
