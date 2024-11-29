#!/bin/python3

import subprocess
import sys
import socket
from datetime import date
import multiprocessing
import statistics

class bench_conf():
  benchmark=None
  num_threads=None
  num_rounds=None
  time=None
  extra_arguments=None
  steal_retries=None
  list_bytes=None
  clear=None #whether to clear the allocator in each run
  shuffle=None #whether to shuffle memory in the allocator before run

  def __init__(self, benchmark,\
    extra_arguments="",\
    num_threads=multiprocessing.cpu_count(),\
    num_rounds=10,\
    time=0.4,\
    steal_retries=32,\
    list_bytes=(2**18-64),\
    clear=True,\
    shuffle=True,\
    ):
    #get fullpath of file
    self.benchmark=subprocess.getoutput("realpath " + benchmark)
    self.num_threads=num_threads
    self.num_rounds=num_rounds
    self.time=time
    self.extra_arguments=extra_arguments
    self.steal_retries=steal_retries
    self.list_bytes=list_bytes
    self.clear=clear
    self.shuffle=shuffle
    
  def __str__(self):
    return "Benchmark configuration "+\
    "{benchmark="+str(self.benchmark)+\
    ",num_threads="+str(self.num_threads)+\
    ",num_rounds="+str(self.num_rounds)+\
    ",time="+str(self.time)+\
    ",extra_arguments="+self.extra_arguments+\
    ",steal_retries="+str(self.steal_retries)+\
    ",list_bytes="+str(self.list_bytes)+\
    "}"

  def build_cmd(self):
    cmd=""
    cmd+="PARLAY_NUM_THREADS="+str(self.num_threads)+" "
    cmd+="PARLAY_STEAL_RETRIES="+str(self.steal_retries)+" "
    cmd+="PARLAY_LIST_BYTES="+str(self.list_bytes)+" "
    cmd+="numactl -i all "
    cmd+=self.benchmark+" "
    cmd+="-t "+str(self.time)+" "
    cmd+="-r "+str(self.num_rounds)+" "
    cmd+="-p "+str(self.num_threads)+" "
    if self.clear: cmd+="-clear "
    if self.shuffle: cmd+="-shuffle "
    cmd+=self.extra_arguments
    return cmd

  #runs num_rounds times
  def run_once(self):
    out=subprocess.getoutput(self.build_cmd())
    #get throughputs for each run
    throughputs=[float(line.split(',')[-1]) for line in out.split('\n')]
    return throughputs

  def run(self):
    throughputs=self.run_once()
    mean=statistics.mean(throughputs)
    stddev=statistics.stdev(throughputs)
    return mean, stddev, throughputs


n_iters=500

directories=[\
  "$HOME/flock-private/build/benchmark/transactions/",\
  "$HOME/tmp/flock-private/build/benchmark/transactions/",\
]

benchmarks=[\
  "hash_block_lck",\
]

configs=[\
  #("-u 0 -upsert", 64, n_iters, 1.4, 64, 2**18-64),\
  #("-u 5 -upsert", 64, n_iters, 1.4, 64, 2**18-64),\
  #("-u 20 -upsert", 64, n_iters, 1.4, 64, 2**18-64),\
  #("-u 50 -upsert", 64, n_iters, 1.4, 64, 2**18-64),\

  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**18-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**17-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**16-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**15-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**14-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**13-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**12-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**11-64),\
  #("-u 100 -upsert", 64, n_iters, 1.0, 64, 2**10-64),\

  ("-u 100 -upsert -n 100", 64, n_iters, 1.0, 64, 2**18-64),\
  ("-u 100 -upsert -n 1000", 64, n_iters, 1.0, 64, 2**18-64),\
  ("-u 100 -upsert -n 10000", 64, n_iters, 1.0, 64, 2**18-64),\
  ("-u 100 -upsert -n 100000", 64, n_iters, 1.0, 64, 2**18-64),\
  ("-u 100 -upsert -n 100000", 64, n_iters, 1.0, 64, 2**18-64),\
  ("-u 100 -upsert -n 1000000", 64, n_iters, 1.0, 64, 2**18-64),\
  ("-u 100 -upsert -n 10000000", 64, n_iters, 1.0, 64, 2**18-64),\
]

bcs=[]

for dire in directories:
  for bench in benchmarks:
    for conf in configs:
      bcs.append(bench_conf(dire+bench, *conf))

for bc in bcs:
  print(bc)
  mean, stddev, throughputs = bc.run()
  print(mean, stddev)
