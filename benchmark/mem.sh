#!/bin/bash

t=143
sys=mimalloc
bench=btree_lck
n=50000000

file_n="${sys}_${n}"
PARLAY_NUM_THREADS=$t LD_PRELOAD=/home/ajcosta/flock-private/benchmark/lib/lib${sys}.so /home/ajcosta/flock-private/build/benchmark/transactions/${bench} -t 3 -r 1 -p $t -u 100 -n $n -pin -stats > $file_n
cat $file_n | sort | uniq -c | sort -n > ${file_n}_sorted
cat ${file_n}_sorted | tr -s ' ' | cut -d' ' -f2 | uniq -c | tr -s ' ' > ${file_n}_${sys}_${bench}_${n}_res-$(date '+%d-%h-%Y')