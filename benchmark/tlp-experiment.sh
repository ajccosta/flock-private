#!/bin/bash

SCRIPT_DIR=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)

#compile deqalloc with multiple configurations
pushd $SCRIPT_DIR/lib/deqalloc/examples/deqalloc/build;

for i in $(echo "0.0625 0.125 0.25 0.5 1 2 8 16 32 64 128 256 512 1024"); do
  if [ ! -f "$SCRIPT_DIR/lib/libdeqalloc_${i}.so" ]; then
    cmake -DDEFAULT_LIST_BYTES=$(python3 -c "print(int($i*1024/4))") ..; 
    make -j -B; 
    mv $(readlink -f libdeqalloc.so) ../../../../libdeqalloc_${i}.so; 
  fi
done
popd

sz_classes="8, 16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072, 3584, 4096"

get_jemalloc_conf() {
  n=$1
  a=1; 
  tcache_ncached_max_set=$(for i in $(echo $sz_classes | tr -d ,); do echo $a-$i:$((n / i)); a=$((i+1)); done | tr '\n' '|' | sed 's/.$//')
  echo "tcache_ncached_max:$tcache_ncached_max_set,tcache_max:$((32*1024))"
}

#TLPs between 1 and 1024 KiB
for i in $(echo "0.0625 0.125 0.25 0.5 1 2 8 16 32 64 128 256 512 1024"); do
  n=$(python3 -c "print(int($i*1024))") #how many bytes are allowed per size class
  echo "$i KiB"

  MALLOC_CONF=$(get_jemalloc_conf $n) PARLAY_NUM_THREADS=143 LD_PRELOAD=lib/libjemalloc.so /home/ajcosta/flock-private/build/benchmark/transactions/hash_block_lck -t 5 -r 3 -p 143 -u 100 -n 100000000;

  PARLAY_NUM_THREADS=143 LD_PRELOAD=lib/libdeqalloc_${i}.so /home/ajcosta/flock-private/build/benchmark/transactions/hash_block_lck -t 5 -r 3 -p 143 -u 100 -n 100000000;
done
