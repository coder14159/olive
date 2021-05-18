#!/usr/bin/env python3

from pathlib import Path
import pandas as pd
import queue

#
# Update the CPU bind list to use -1 for clients which do not bind to a cpu
#
NO_CPU_BIND = -1

def cpu_bind_list (cpu_list, client_count):

  while len (cpu_list) < client_count:
    cpu_list.insert (0, NO_CPU_BIND)

  return cpu_list

# Return a directory based on the parameters supplied
def output_directory_path (
  base_directory,
  server_queue_size, server_rate, server_message_size,
  client_count, client_prefetch_size = 0):

  server_rate_str = 'max' if server_rate is '0' else str (server_rate)

  path = Path (base_directory) \
            / 'server_queue_size'    / str (server_queue_size) \
            / 'server_rate'          / server_rate_str  \
            / 'server_message_size'  / str (server_message_size) \
            / 'client_count'         / str (client_count) \
            / 'client_prefetch_size' / str (client_prefetch_size)

  return path

# Return a human readable message throughput string
def throughput_messages_to_pretty (rate):
    if rate == 'max':
        return rate
    if rate == '0':
        return 'max'

    K = 1.0e3
    M = 1.0e6
    G = 1.0e6

    float_rate = float (rate)

    if float_rate > G:
        return str (round (float_rate / G, 1)) + " G msgs/s"
    if float_rate > M:
        return str (round (float_rate / M, 1)) + " M msgs/s"
    if float_rate > K:
        return str (round (float_rate / K)) + " K msgs/s"

    return str (round (float_rate)) + " msgs/s"

def load_performance_data (args, filename, transpose_data=False):
  legend_texts = []
  legend_prefix_texts = None
  title_text = set ()
  dataframe = None
  column_count = 0

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

              data_directory = output_directory_path (dir,
                                              server_queue_size,
                                              server_rate,
                                              message_size,
                                              client_count,
                                              client_prefetch_size)

              file_path = Path (data_directory) / filename

              if file_path.exists () == False:
                print (str (file_path) + " does not exist")
                continue

              df = None
              print ('loading: ' + str (file_path))
              df = pd.read_csv (file_path)

              if 'throughput-interval' in filename:
                if dataframe is None:
                  print (df.head ())

                  dataframe = pd.DataFrame (df['megabytes_per_sec'])
                  print (dataframe)

                else:
                  print (df.head ())
                  column_count += 1
                  dataframe = pd.concat ([dataframe, df['megabytes_per_sec']], axis=1, ignore_index=True)
                  print (dataframe)

              if 'latency-summary' in filename:
                df = df.transpose ()

                if dataframe is None:
                  dataframe = df
                else:
                  column_count += 1
                  dataframe[column_count] = df

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

  return dict (dataframe=dataframe,
               legend_texts=legend_texts,
               title_texts=title_text)

#
# Get latency summary data
#
def get_latency_summary_data (args):

  latency_data = load_performance_data (args,'latency-summary.csv',
                                        transpose_data=True)

  print (latency_data)
  print (latency_data['dataframe'])

  return dict (latency_summaries=latency_data['dataframe'],
               legend_texts=latency_data['legend_texts'],
               title_texts=latency_data['title_texts'])

def get_throughput_interval_data (args):

  throughput_data = load_performance_data (args, 'throughput-interval.csv')

  print (throughput_data)
  print (throughput_data['dataframe'])

  return dict (throughput_intervals=throughput_data['dataframe'],
               legend_texts=throughput_data['legend_texts'],
               title_texts=throughput_data['title_texts'])

