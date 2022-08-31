# `Olive`

The Olive library implements inter-process communication between a producer process and one or more consumers on a single machine over named shared memory. The relevant tools are prefixed by SPMC (single producer/multiple consumers).

The goal of the project is to enable a producer process to send messages, with a zero message drop guarantee, to one or more consumers  while holding latencies as low as possible and to support high throughput. An example use case might be distributing data to micro services with a zero message drop guarantee.

In order to compare the performance of the SPMC code an implementation of the *boost::lockfree::spsc_queue* (SPSC) communicating over shared memory is also implemented.

To build, unit test and generate performance test data see [build and test the Olive binaries](./README-build-test.md).

---

## `Performance plotting`

## Generating performance data examples
Using the script *run_performance_tests.py* a range of performance test data can be generated.

Generate performance data for the SPMC implementation. This invocation assumes the server being invoked makes use of *Profile Guided Optimisation* available on Linux - see [here](./README-build-test.md).

Here we generate data for a range of queue sizes, throughput rates and client counts.

```
scripts/run_performance_tests.py --tool_type spmc --timeout 60 --server_cpu 1 --server_queue_size_list 512 65536 1048576 --server_message_size 32 --server_rate_list 1000 max  --client_stats latency throughput interval --client_directory ~/data/spmc/v1 --memory_name smem --client_count_list 1 2 --client_cpu_list 2 3 --server_pgo
```

Comparison performance data is generated using a *boost::lockfree::spsc_queue* based implementation.

```
scripts/run_performance_tests.py --tool_type spsc --timeout 60 --server_cpu 1 --server_queue_size_list 512 65536 1048576 --server_message_size 32 --server_rate_list 1000 max  --client_stats latency throughput interval --client_directory ~/data/spsc/v1 --memory_name smem --client_count_list 1 2 --client_cpu_list 2 3 --server_pgo
```

# Plotting performance data
Having generated performance data, one can compare the throughput and latency for the different performance test configurations using the plotting scripts.

## Small queue size
Both the SPMC and SPSC implementations exhibit lower latency values for smaller queue sizes but also lower maximum throughputs.

<img src="images/client_interval_latency-spmc-vs-spsc-rate-1000-clients-1-queue_size-128.png" width=80% height=20%>

## Large queue size
When using a larger queue, the SPMC implementation prioritises throughput at the expense of latency values.

The maximum throughput for a single SPSC client is roughly one third of the equivalent SPMC implementation and is roughly equal to the SPMC implementation running two clients.

<img src="images/client_interval_latency-spmc-vs-spsc-rate-max-clients-1-2-queue_size-2048000.png" width=80% height=20%>

