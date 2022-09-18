### `Build and test the Olive binaries`
---

To build all binaries and run tests
```
$ make -j2 all
$ make test
```

View the [Makefile](Makefile) for targets to build individual or groups of targets.

---

## Sample use cases

The sample SPMC (single producer, multiple consumer) tools illustrate how to use the Olive library.

The command below starts a server which creates a named shared memory space and places a queue within it (or opens a queue of the same name if it already exists). Messages are then sent to the queue at a configured rate. Here, the rate is 1000 messages/second. Not setting a throughput rate on the command line, or using '0' implies maximum throughput.

In the example below, the message size selected is similar to that of a typical market data message size. The small queue size is sufficient for a lower throughputs and helps to keep latency values small. The value set to the **name** field will be the name of the shared memory to be created and/or opened.

```
$ build/x86_64/bin/spmc_server --cpu 1 --name smem --message_size 32 --queue_size 100 --rate 1000 --log_level INFO
```
Run a client to consume messages from the queue. The optional stats argument outputs performance statistics to stdout.
```
$ timeout 30 build/x86_64/bin/spmc_client --name smem --log_level INFO --cpu 2 --stats latency,throughput
<snip>
65 KB/s    1 K msgs/s
percentile latency
---------- -------
min         102 ns
1           119 ns
10          133 ns
25          142 ns
50          157 ns
75          190 ns
80          188 ns
90          234 ns
95          250 ns
99          465 ns
99.5        668 ns
99.6        668 ns
99.7        668 ns
99.8        668 ns
99.9        668 ns
99.95       668 ns
99.99       668 ns
max         818 ns
```
An understanding of likely message sizes and throughput values enables one to optimise the best queue size for a given use case. Larger queue sizes enable higher throughputs at a cost of increased latency values and vise versa.

For example
```
$ build/x86_64/bin/remove_shared_memory --names smem
```
```
$ build/x86_64/bin/spmc_server --cpu 1 --name smem --message_size 32 --queue_size 102400 --log_level INFO
```
```
$ timeout 30 build/x86_64/bin/spmc_client --name smem --log_level INFO --cpu 2 --stats latency,throughput
<snip>
1.1 GB/s   19 M msgs/s
percentile latency
---------- -------
min          89 ns
1           128 ns
10          150 ns
25          171 ns
50          202 ns
75          278 ns
80          312 ns
90          630 ns
95            1 us
99            4 us
99.5          6 us
99.6          6 us
99.7          7 us
99.8          8 us
99.9          9 us
99.95        11 us
99.99        12 us
max          12 us
```
---

## PGO
Utilising **Profile Guided Optimisation** (PGO) can improve throughput and latency values.

On my machine, enabling the **tuned-adm** profile *latency-performance* improves both latency and throughput performance, but only when applied to the *spmc_server* and not to the *spmc_client*.

To enable tuned-adm
```
$ tuned-adm active
No current active profile.
$ tuned-adm profile latency-performance
$ tuned-adm active
Current active profile: latency-performance
```

Run the **build_run_spmc_pgo.py** script to generate PGO binaries.

Setting the queue size to a relatively small size when using this script results in the generation of PGO binary with a higher throughput, but interestingly, only when applied to the **spmc_server** and not the **spmc_client**.

```
$ scripts/build_run_spmc_pgo.py --build_jobs 2 --log_level INFO --run_time 30 --memory_name smem --server_queue_size 500 --server_message_size 32 --server_rate 0 --client_stats latency throughput interval
```

```
$ build/x86_64.pgo_release/bin/spmc_server --cpu 1 --name smem --message_size 32 --queue_size 102400 --log_level INFO
```
```
$ timeout 30 build/x86_64/bin/spmc_client --name smem --log_level INFO --cpu 2 --stats latency,throughput
<snip>
2.1 GB/s   35 M msgs/s
percentile latency
---------- -------
min         169 ns
1            22 us
10           23 us
25           23 us
50           23 us
75           24 us
80           25 us
90           26 us
95           26 us
99           28 us
99.5         29 us
99.6         29 us
99.7         30 us
99.8         30 us
99.9         31 us
99.95        32 us
99.99        39 us
max          45 us
```
---
An implementation of a client and server based on **boost::lockfree::spsc_queue** over shared memory has been implemented to provide a reference comparision for the SPMC client and server.

I find that maximum throughput for the SPSC implementation for this use case is roughly 1/3 of the SPMC implementation.

Note that currently, the SPSC client count must be set on starting the SPSC server and requires all clients to be running before data can be sent.

```
$ build/x86_64/bin/spsc_server --cpu 1 --name smem --message_size 32 --queue_size 102400 --log_level INFO --clients 1
```
```
build/x86_64/bin/spsc_client --name smem --log_level INFO --cpu 2 --stats latency,throughput
<snip>
730 MB/s   12 M msgs/s
percentile latency
---------- -------
min          81 ns
1           130 ns
10          167 ns
25          187 ns
50          214 ns
75          249 ns
80          264 ns
90          278 ns
95          639 ns
99            7 us
99.5         11 us
99.6         11 us
99.7         13 us
99.8         13 us
99.9         14 us
99.95        16 us
99.99        22 us
max          32 us
```

Reading the data in batches improves throughput at the expence of latency, much like the SPMC implementation.