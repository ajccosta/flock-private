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
  time_step=None #How much to increase time each time a stddev run is done
  extra_arguments=None

  def __init__(self, benchmark,\
    extra_arguments="",\
    num_threads=multiprocessing.cpu_count(),\
    num_rounds=10,\
    time=0.4,\
    time_step=0):
    self.benchmark=benchmark
    self.num_threads=num_threads
    self.num_rounds=num_rounds
    self.time=time
    self.extra_arguments=extra_arguments
    self.time_step=time_step
    
  def __str__(self):
    return "Benchmark configuration "+\
    "{benchmark="+str(self.benchmark)+\
    ",num_threads="+str(self.num_threads)+\
    ",num_rounds="+str(self.num_rounds)+\
    ",time="+str(self.time)+\
    ",extra_arguments="+self.extra_arguments+\
    "}"

  def build_cmd(self):
    cmd=""
    cmd+="PARLAY_NUM_THREADS="+str(self.num_threads)+" "
    cmd+="numactl -i all "
    cmd+="./"+self.benchmark+" "
    cmd+="-t "+str(self.time)+" "
    cmd+="-r "+str(self.num_rounds)+" "
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

  #runs num_rounds times until the relative stddev is under rel_stddev_perc_threshold
  def run_relative_stddev(self, rel_stddev_perc_threshold:float):
    all_throughputs=[]
    curr_relative_stddev=100
    while curr_relative_stddev >= rel_stddev_perc_threshold:
      all_throughputs.extend(self.run_once())
      stddev=statistics.stdev(all_throughputs)
      mean=statistics.mean(all_throughputs)
      curr_relative_stddev=100*(stddev/mean)

      self.time+=self.time_step #increase time of test, to attempt to stabilize tests

      #filter outliers
      num_quartiles=20
      quantiles=statistics.quantiles(all_throughputs, n=num_quartiles)
      Q1_thresh=quantiles[0]
      QLast_thresh=quantiles[num_quartiles-2]
      all_throughputs=list(filter(lambda a : a >= Q1_thresh and a <= QLast_thresh, all_throughputs))

      print("relative stddev:", curr_relative_stddev, "stddev:", stddev, "mean:", mean)
      #print(all_throughputs)

    return mean, stddev, all_throughputs

bc = bench_conf("hash_block_lck", "-u 100 -upsert", 64, 10, 1, 0)
print(bc)
res=bc.run_relative_stddev(1.0)
print(res)
