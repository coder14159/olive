# `Olive`

The Olive library implements inter-process communication between a producer process and one or more consumers on a single machine over named shared memory. The relevant tools are prefixed by **SPMC** (single producer/multiple consumers).

The goal of the project is to enable a producer process to send messages, with a zero message drop guarantee, to one or more consumers  while holding latencies as low as possible and to support high throughput.

An example use case for this software could be distributing data between micro services with a zero message drop guarantee.

Shared memory implementations of producer/consumer based around **boost::lockfree::spsc_queue** (**SPSC**) executables is also implemented to provide a comparison for the SPMC code.

To build, unit test and generate performance test data see [build and test the Olive binaries](./README-build-test.md).

---

## `Performance plots`

### `Generate sample performance data`
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
---
### `Plot performance data`
Having generated performance data, one can compare throughput and latency information for different performance test configurations by using the plotting scripts ***plot_latency_intervals.py*** and ***plot_latency_summary.py***

#### `Using a smaller queue size`

Using smaller queue sizes is beneficial for generating lower latencies. Conversely, maximum throughput values are reduced when working with smaller queues.

The plot below was generated using a throighput limited to 1000 msgs/sec. Both the SPMC and SPSC implementations exhibit lower latency values for smaller queue sizes.

Maximum latency values for the SPMC implementation are lower than the equivalent values for the SPSC design.
However, the throughput graph for SPMC is less smooth than SPSC. This is due to the SPMC data being consumed in batches.

<img src="images/client_interval_latency-spmc-vs-spsc-rate-1000-clients-1-queue_size-512.png" width=80% height=20%>

When testing at maximum throughput, the SPMC implementation continues to exhibit lower latencies as well as a much higher throughput than the SPSC implementation.

<img src="images/client_interval_latency-spmc-vs-spsc-rate-max-clients-1-queue_size-512.png" width=80% height=20%>

**Performance Summaries**

The performance summaries below show the full latency profile using a log scale to more easily visualise the data.

At a lower throughput rate, the SPMC implementation demonstrates similar latency profile to SPSC up to around 90%. Above 90% the SPMC implementation exhibits a better latency profile.

<img src="images/client_summary_latency-spmc-vs-spsc-rate-1000-clients-1-2-queue_size-512.png" width=80% height=20%>

Maximum throughput is much higher and latency is lower for the SPMC implementation than for SPSC.

<img src="images/client_summary_latency-spmc-vs-spsc-rate-max-clients-1-2-queue_size-512.png" width=80% height=20%>


#### `Using a larger queue size`

When limited to a low throughput, the SPMC implementation displays a lower maximum latency than the SPSC implementation. However, the 99th pecentiles are largely similar for both implementations.

<img src="images/client_interval_latency-spmc-vs-spsc-rate-1000-clients-1-2-queue_size-65536.png" width=80% height=20%>

Using larger queues for the SPMC implementation results in a bias towards throughput at the expense of increased latency values.

<img src="images/client_interval_latency-spmc-vs-spsc-rate-max-clients-1-2-queue_size-65536.png" width=80% height=20%>

**Performance Summaries**

The performance summaries below show the full latency profile using a log scale.

Limiting throughput to 1000 messages/second.

<img src="images/client_summary_latency-spmc-vs-spsc-rate-1000-clients-1-2-queue_size-65536.png" width=80% height=20%>

When throughput is not limited, the SPMC implementation has a much higher throughput than equivalent SPSC configuration. Latency is also generally higher, due to the batching behaviour of the SPMC code.

<img src="images/client_summary_latency-spmc-vs-spsc-rate-max-clients-1-2-queue_size-65536.png" width=80% height=20%>
