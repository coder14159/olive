#!/bin/bash

script_dir=$(dirname $(readlink -f $0))
base_dir=$(dirname $script_dir)
cur_dir=`pwd`

memory_name=test_memory_spmc
bin_dir=build/x86_64/bin
profile_bin_dir=build/x86_64.pgo_profile/bin
release_bin_dir=build/x86_64.pgo_release/bin

# Use environment variables to override default options
jobs=${JOBS:-1}
queue_size=${QUEUE_SIZE:-20480}
prefetch_size=${PREFETCH_SIZE:-0}

echo "# Build executable for removing shared memory"
cd $base_dir && make ${bin_dir}/remove_shared_memory --jobs $jobs

echo "# Removing shared memory $memory_name"
cd $base_dir && ${bin_dir}/remove_shared_memory --names $memory_name

echo "# Build executables for generating profile data"
cd $base_dir && \
   make PGO_PROFILE=1 $profile_bin_dir/spmc_server \
                      $profile_bin_dir/spmc_client --jobs $jobs

echo "# Run the tests generating profile guiding data"
cd $base_dir && \
   $profile_bin_dir/spmc_server --cpu 1 --name $memory_name \
                                --message_size 32 --queue_size $queue_size \
                                --rate 0&

sleep 2

cd $base_dir && \
   $profile_bin_dir/spmc_client --cpu 2 --name $memory_name \
                                --log_level INFO \
                                --stats latency,throughput,interval \
                                --prefetch_cache $prefetch_size&

sleep 15

pkill spmc_client
pkill spmc_server

echo "# Removing shared memory $memory_name"
cd $base_dir && ${bin_dir}/remove_shared_memory --names $memory_name

echo "# Build profile guided release"
cd $base_dir && \
   make PGO_RELEASE=1 $release_bin_dir/spmc_server \
                      $release_bin_dir/spmc_client --jobs $jobs

echo "# Run the tests using profile guided data"
cd $base_dir && \
   $release_bin_dir/spmc_server --cpu 1 --name $memory_name \
                                --message_size 32 --queue_size $queue_size \
                                --rate 0&

sleep 2

cd $base_dir && \
   $release_bin_dir/spmc_client --cpu 2 --name $memory_name \
                                --log_level INFO \
                                --stats latency,throughput,interval \
                                --prefetch_cache $prefetch_size&

sleep 15

pkill spmc_client
pkill spmc_server
