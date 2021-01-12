#!/bin/bash

script_dir=$(dirname $(readlink -f $0))
base_dir=$(dirname $script_dir)
cur_dir=`pwd`

memory_name=test_memory
bin_dir=build/x86_64/bin
profile_bin_dir=build/x86_64.pgo_profile/bin
release_bin_dir=build/x86_64.pgo_release/bin

echo "# Build executable for removing shared memory"
cd $base_dir && make ${bin_dir}/remove_shared_memory

echo "# Removing shared memory $memory_name"
cd $base_dir && ${bin_dir}/remove_shared_memory --names $memory_name

echo "# Build executables for generating profile data"
cd $base_dir && make PGO_PROFILE=1 -j2 $profile_bin_dir/spsc_server
cd $base_dir && make PGO_PROFILE=1 -j2 $profile_bin_dir/spsc_client

echo "# Run the tests generating profile guiding data"
cd $base_dir && $profile_bin_dir/spsc_server --cpu 1 --name $memory_name --message_size 32 --queue_size 20480 --rate 0 --clients 1&

sleep 1

cd $base_dir && timeout 15 ./$profile_bin_dir/spsc_client --cpu 2 --name $memory_name --log_level INFO --stats latency,throughput,interval

pkill spsc_server

echo "# Removing shared memory $memory_name"
cd $base_dir && ${bin_dir}/remove_shared_memory --names $memory_name

echo "# Rebuild profile guided release"
cd $base_dir && make PGO_RELEASE=1 -j2 $release_bin_dir/spsc_server
cd $base_dir && make PGO_RELEASE=1 -j2 $release_bin_dir/spsc_client

echo "# Run the tests generating profile guided data"
cd $base_dir && $release_bin_dir/spsc_server --cpu 1 --name $memory_name --message_size 32 --queue_size 20480 --rate 0 --clients 1&

sleep 1

cd $base_dir && ./$release_bin_dir/spsc_client --cpu 2 --name $memory_name --log_level INFO --stats latency,throughput,interval
cd $base_dir && ./$release_bin_dir/spsc_client --cpu 2 --name $memory_name --log_level INFO --stats latency,throughput,interval

sleep 15

pkill spsc_client
pkill spsc_server
