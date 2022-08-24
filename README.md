# `Olive`

The Olive library implements inter-process communication between a producer process and multiple consumers on a single machine over named shared memory. These prefixed by SPMC (single producer/multiple consumers).

The goal of the project is to send the messages with a zero message drop guarantee while holding latencies as low as possible and to support high throughput. An example use case might be for distributing market data from a stock exchange.

An implementation of *boost::lockfree::spsc_queue* (SPSC) communicating over shared memory is provided for performance comparision.

To build, unit test and generate performance test data see [build and test the Olive binaries](./README-build-test.md).

---

## Performance plots

Having generated performance data, the following plots illustrate latency and throughput performance for SPMC and SPSC implementations

Using a smaller queue size exhibits lower latency values but a lower maximum throughput.
![Alt](images/client_interval_latency-spmc-vs-spsc-rate-1000-clients-1-queue_size-128.png "SPMC vs SPSC with small queue size")

A larger queue size improves throughput at the cost of higher latencies.


