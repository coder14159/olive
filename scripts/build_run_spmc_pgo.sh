#!/bin/bash

script_dir=$(dirname $(readlink -f $0))
base_dir=$(dirname $script_dir)
cur_dir=`pwd`

# Use environment variables to set command line parameters
jobs=${JOBS:-1}
queue_size=${QUEUE_SIZE:-20480}
run_time=${RUN_TIME:-17}

memory_name=test_memory_spmc
bin_dir=build/x86_64/bin
profile_bin_dir=build/x86_64.pgo_profile/bin
release_bin_dir=build/x86_64.pgo_release/bin

echo "# Build executable for removing shared memory"
cd $base_dir && make remove_shared_memory --jobs $jobs

echo "# Removing shared memory $memory_name"
cd $base_dir && $bin_dir/remove_shared_memory --names $memory_name

echo "# Build standard release executables"
cd $base_dir && make spmc --jobs $jobs

echo "# Build executables for generating profile data"
cd $base_dir && \
   make PGO_PROFILE=1 $profile_bin_dir/spmc_server \
                      $profile_bin_dir/spmc_client --jobs $jobs

echo "# Run the tests generating profile guided data"
cd $base_dir && \
   $profile_bin_dir/spmc_server --cpu 1 --name $memory_name \
                                --message_size 32 --queue_size $queue_size \
                                --rate 0&

sleep 1
cd $base_dir && \
   $profile_bin_dir/spmc_client --cpu 2 --name $memory_name \
                                --log_level INFO \
                                --stats latency,throughput,interval&
sleep $run_time

pkill spmc_server
pkill spmc_client

echo "# Remove shared memory $memory_name"
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

sleep 1

cd $base_dir && \
   $release_bin_dir/spmc_client --cpu 2 --name $memory_name \
                                --log_level INFO \
                                --stats latency,throughput,interval&
sleep $run_time

pkill spmc_server
pkill spmc_client

echo "# Removing shared memory $memory_name"
cd $base_dir && ${bin_dir}/remove_shared_memory --names $memory_name