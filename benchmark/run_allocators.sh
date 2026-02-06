#!/bin/bash
alloc_dir=lib/allocators

if [ ! -d "$alloc_dir" ]; then 
  ./build.sh
fi

#:numa means the allocator will be run with numactl -i all
#:df is ignored here
# We chose which allocator uses what based on experimental results.
# It is highly machine dependent, and some allocators have no clear
# best configuration, as the best changes from data structure to data structure.
allocators="deqalloc mimalloc jemalloc:numa:df snmalloc hoard:numa tcmalloc::df tbbmalloc::df lockfree:numa:df rpmalloc::df"
runs=5
update_percs="1 5 50 90 100"
#different data structures will require different sizes"
#small-sized data structures
rideables_sizes="btree_lck:5000 hash_block_lck:5000 leaftree_lck:5000 skiplist_lck:5000 arttree_lck:5000 list_lck:500"
#medium-sized data structures
rideables_sizes+=" btree_lck:200000 hash_block_lck:200000 leaftree_lck:200000 skiplist_lck:200000 arttree_lck:200000 list_lck:2000"
#medium-large-sized data structures
rideables_sizes+=" btree_lck:20000000 hash_block_lck:20000000 leaftree_lck:2000000 skiplist_lck:2000000 arttree_lck:2000000 list_lck:10000"
#large-sized data structures
rideables_sizes+=" btree_lck:200000000 hash_block_lck:200000000 leaftree_lck:20000000 skiplist_lck:20000000 arttree_lck:20000000 list_lck:10000"

update_percs="100"

fmt="%-12s %-10s %-15s %-10s %-6s %s"
printf "$fmt\n" "allocator" "update%" "ds" "key_size" "numa" "results"
for rideable_size in $(echo "$rideables_sizes"); do
  for update_perc in $(echo "$update_percs"); do
    for allocator_wnuma in $(echo "$allocators"); do
      rideable=$(echo $rideable_size | cut -d: -f1)
      size=$(echo $rideable_size | cut -d: -f2)
      #remove :numa from allocator
      allocator=$(echo $allocator_wnuma | cut -d: -f1)
      numa_suffix=$(echo $allocator_wnuma | cut -d: -f2)
      if [[ "$numa_suffix" = numa ]]; then
        use_numa=true
        numa_suffix="numactl -i all"
      else
        use_numa=false
        numa_suffix=""
      fi
      #decide whether to use numactl or not
      printf "$fmt" "$allocator" "$update_perc" "$rideable" "$size" "$use_numa" "[ "
      tp_avg=0
      memusage_avg=0
      raw=""
      #takes -n as half the total key size
      res=$(PARLAY_NUM_THREADS=$(nproc) LD_PRELOAD=${alloc_dir}/lib${allocator}.so \
      /usr/bin/time -f "linebreak%M KiloBytes" $numa_suffix \
      ../build/benchmark/transactions/${rideable} \
      -p $(nproc) -u $update_perc -n $(( $size / 2 )) -t 5 -r $runs 2>&1)
      res=$(echo -e "$res" | sed 's/linebreak/#/g' | tr '#' '\n')
      memusage=$(echo -e "$res" | grep "KiloBytes" | cut -d' ' -f1)
      tps=$(echo -e "$res" | grep -v "KiloBytes" | rev | cut -d, -f1 | rev | tr '\n' ' ' | tr -s ' ')
      tp_avg=$(python -c "print(("$(echo -e $tps | tr ' ' +)" +0)/$runs)")
      printf "$tps] $tp_avg, ${memusage} KB\n"
    done
  done
done