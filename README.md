# `Olive`

The Olive library implements a method of inter-process communication between processes on a single machine over named shared memory.

The goal of the project is to send data messages from a server process to 1 or more clients processes with *zero message drops guaranteed* and holding latencies as low as possible.

## Building Olive
```
$ make -j all
```

## Sample usage

The sample spmc (single producer, multiple consumer) tools illustrate how to use the Olive library.

First delete named shared memory which will be used.

```
$ ./build/x86_64/bin/remove_shared_memory --names spmc
```

The command below starts a server which creates (or opens existing) named shared memory and places a queue within it. Messages are then sent to the queue at a configured rate. Here, the rate is 1000 messages/second. A command line value of '0' indicates maximum throughput.

In the example below a message size similar to that of a typical market data message size is set. The small queue size is sufficient for relatively low throughputs and will help keep latency values low.

```
$ build/x86_64/bin/spmc_server --cpu 1 --name spmc --message_size 32 --queue_size 100 --rate 1000 --log_level INFO
```
Next run a client which consumes messages from the queue. The optional stats argument outputs performance statistics to stdout.
```
$ timeout 30 build/x86_64/bin/spmc_client --name spmc --log_level INFO --cpu 2 --stats latency,throughput

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
An understanding of likely message sizes and throughput enables one to optimise the best queue size for your use case. Larger queue sizes permits a higher throughput at a cost of increased latency values and vise versa.

For example
```
$ ./build/x86_64/bin/remove_shared_memory --names spmc; build/x86_64/bin/spmc_server --cpu 1 --name spmc --message_size 32 --queue_size 102400 --rate 0 --log_level INFO

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

On my machine, I found that utilising the **tuned-adm** tool for latency optimisation improved throughput at the expense of latency.

```
$ sudo tuned-adm profile latency-performance
```

```
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
