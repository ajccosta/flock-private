#!/bin/python3

import subprocess
import multiprocessing
import statistics
import re
import os
import sys
from datetime import datetime

if len(sys.argv) < 2 or \
  ("-perf" not in sys.argv and "-mem" not in sys.argv):
  print(f"""\
Usage: python3 bench.py [-perf|-mem]
  -perf: run performance benchmarks
  -mem: run memory tracing benchmarks
  Specifying both -perf and -mem will run both benchmark types\
        """)

#regex to filter out of results
#Mesh segfaults but seems to run fine until the end
filter_out="\n*duration: [0-9]+(?:.[0-9]*)?\n|\n*segfault.*\nAborted|\n*Segmentation.*|Some.*cleared!\n|\nSome.*cleared!|\nbad.*is 0.[0-9]+"

base_directory=subprocess.getoutput("realpath "+re.sub(os.path.basename(__file__), "", os.path.realpath(__file__))+"/..")+"/"
bin_directory="build/benchmark/transactions/"

docker_base_directory="/home/ubuntu"

allocator_pairs = { \
  #allocator_name: shared lib relative path
  "jemalloc":           "lib/libjemalloc.so", \
  "mimalloc":           "lib/libmimalloc.so", \
  "mesh":               "lib/libmesh.so",     \
  "hoard":              "lib/libhoard.so",    \
  "deqalloc":           "lib/libdeqalloc.so", \
  "scalloc":            "lib/libscalloc.so",  \
  "tcmalloc":           "lib/libtcmalloc.so", \
}

def detectCPUs():
    """
     Detects the number of CPUs on a system. Cribbed from pp.
     """
    # Linux, Unix and MacOS:
    if hasattr(os, "sysconf"):
       if "SC_NPROCESSORS_ONLN" in os.sysconf_names :
           # Linux & Unix:
           ncpus = os.sysconf("SC_NPROCESSORS_ONLN")
           if isinstance(ncpus, int) and ncpus > 0:
               return ncpus
       else: # OSX:
           return int(os.popen2("sysctl -n hw.ncpu")[1].read())
    # Windows:
    if os.environ.has_key("NUMBER_OF_PROCESSORS"):
           ncpus = int(os.environ["NUMBER_OF_PROCESSORS"]);
           if ncpus > 0:
               return ncpus
    return 1 # Default

class bench_conf():
  bench_type=None
  allocator=None
  allocator_lib=None
  benchmark=None
  num_threads=None
  num_rounds=None
  time=None
  extra_arguments=None
  steal_retries=None
  list_bytes=None
  replay=None #whether we are doing trace replay
  clear=None #whether to clear the allocator in each run
  dense=None 
  warmup=None
  timestamp=None
  def __init__(self,\
    allocator,\
    benchmark,\
    bench_type,\
    extra_arguments="",\
    num_threads=multiprocessing.cpu_count(),\
    num_rounds=10,\
    time=0.4,\
    steal_retries=32,\
    list_bytes=(2**18-64),\
    replay=False,\
    clear=False,\
    dense=False,\
    warmup=True,\
    ):
    #get fullpath of file
    self.bench_type=bench_type
    self.allocator=allocator
    self.allocator_lib=allocator_pairs[allocator]
    self.benchmark=benchmark
    self.num_threads=num_threads
    self.num_rounds=num_rounds
    self.time=time
    self.extra_arguments=extra_arguments
    self.steal_retries=steal_retries
    self.list_bytes=list_bytes
    self.replay=replay
    self.clear=clear
    self.dense=dense
    self.warmup=warmup

  def get_timestamp(self):
    if self.timestamp == None:
      self.timestamp = datetime.today().strftime('%d%m%y-%H%M%S')
    return self.timestamp
    
  def __str__(self):
    if self.bench_type == "perf":
      cmd, env = self.build_cmd()
      return env + cmd
    elif self.bench_type == "mem":
      file_path=f"/benchmark/traces/{self.allocator}/{self.benchmark}/{self.get_timestamp()}.memusage"
      cmd=self.mstat_build_cmd(file_path)
      return cmd
    else:
      return "error"

  def build_cmd(self):
    #environment variables
    env=""
    env+="PARLAY_NUM_THREADS="+str(self.num_threads)+" "
    env+="PARLAY_LIST_BYTES="+str(self.list_bytes)+" "
    env+="LD_PRELOAD="+self.allocator_lib+" "
    #actual command
    cmd=""
    #cmd+="numactl -i all "
    cmd+=f"{base_directory}{bin_directory}{self.benchmark} "
    cmd+="-t "+str(self.time)+" "
    cmd+="-r "+str(self.num_rounds)+" "
    cmd+="-p "+str(self.num_threads)+" "
    if self.clear: cmd+="-clear "
    if not self.warmup: cmd+="-no_warmup_round "
    if self.clear: cmd+="-dense "
    cmd+=self.extra_arguments
    return cmd, env

  #runs num_rounds times
  def run_once(self):
    cmd, env = self.build_cmd()
    out=subprocess.getoutput(f"{env} {cmd}")
    out=re.sub(filter_out, "", out)
    #get throughputs for each run
    try:
      #throughput in million ops per second
      throughputs=[float(line.split(',')[-1]) for line in out.split('\n')]
      #memory in mebibytes
      memory=[float(re.sub(r'\D', '', line.split(',')[-2])) for line in out.split('\n')]
    except Exception as e:
      print(e)
      #error, but continue running
      throughputs=[-1]
      memory=[-1]
    return throughputs, memory

  def run(self):
    throughputs,memory=self.run_once()
    throughput_mean=statistics.mean(throughputs)
    memory_max=max(memory)
    if len(throughputs) > 1:
      stddev = statistics.stdev(throughputs)
    else:
      stddev = 0
    return throughput_mean, memory_max, stddev, throughputs, memory


  #mstat docker functions

  def mstat_build_cmd(self, file_path):
    cmd="mstat "
    cmd+=f"-o {docker_base_directory}/{file_path} "
    cmd+="-freq 120 "
    cmd+=f"-env PARLAY_NUM_THREADS={self.num_threads} "
    cmd+=f"-env PARLAY_LIST_BYTES={self.list_bytes} "
    cmd+=f"-env LD_PRELOAD={docker_base_directory}/benchmark/{self.allocator_lib} "
    #cmd+=f"-env MALLOC_CONF=\"tcache_ncached_max:1-8388608:1,tcache_max:1\" "
    cmd+=f"{docker_base_directory}/{bin_directory}{self.benchmark} "
    cmd+=f"-t {self.time} "
    cmd+=f"-r {self.num_rounds} "
    cmd+=f"-p {self.num_threads} "
    if self.clear: cmd+="-clear "
    cmd+=self.extra_arguments+" "
    cmd+="-no_warmup_round "
    return cmd

  def docker_killall(self):
    #kill running docker containers
    subprocess.getoutput("docker kill " + subprocess.getoutput("docker ps -aq"))

  def docker_start_container(self):
    user_id=subprocess.getoutput("id -u")
    group_id=subprocess.getoutput("id -g")
    dkr_start=f"""
      docker run -u {user_id}:{group_id} -dit --privileged -v {base_directory}:{docker_base_directory} -w {docker_base_directory} --rm ajcostafct/mstat /bin/bash;
    """
    container_id=subprocess.getoutput(dkr_start)
    return container_id

  def docker_prepare(self):
    self.docker_killall()
    container_id=self.docker_start_container()
    return container_id

  def mstat_run(self):
    container_id=self.docker_prepare()
    #create dir if it does not exist
    subprocess.getoutput(f"mkdir -p {base_directory}/benchmark/traces/{self.allocator}/{self.benchmark}")

    file_path=f"/benchmark/traces/{self.allocator}/{self.benchmark}/{self.get_timestamp()}.memusage"
    cmd=self.mstat_build_cmd(file_path)

    #run mstat
    subprocess.getoutput(f"docker exec {container_id} {cmd}")

    #write command to output file
    with open(f"{base_directory}/{file_path}", "a+") as f:
      f.write("\n")
      f.write(cmd)
      f.write("\n")


#number of iterations and time for performance benchmarks
n_iters=3
time=10

#number of iterations and time for memory usage benchmarks
mem_n_iters=10
mem_time=25

benchmarks=[\
  "arttree_lck",\
  "btree_lck",\
  "hash_block_lck",\
  "skiplist_lck",\
  #"arttree_lf",\
  #"btree_lf",\
  #"hash_block_lf",\
  #"skiplist_lf",\
]

allocators=["deqalloc", "mimalloc", "hoard", "jemalloc", "mesh", "tcmalloc", "scalloc"]

ncpus = detectCPUs()

#configurations for performance benchmark
perf_configs=[\
  ##Change write %
  #("perf", "-u 100", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 90", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 80", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 70", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 60", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 50", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 40", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 30", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 20", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 10", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 0", ncpus - 1, n_iters, time, 64, 2**18),\

  ##Change number of threads
  #("perf", "-u 100", ncpus / 64, n_iters, time, 64, 2**18),\
  #("perf", "-u 100", ncpus / 32, n_iters, time, 64, 2**18),\
  #("perf", "-u 100", ncpus / 16, n_iters, time, 64, 2**18),\
  #("perf", "-u 100", ncpus / 8, n_iters, time, 64, 2**18),\
  #("perf", "-u 100", ncpus / 4, n_iters, time, 64, 2**18),\
  #("perf", "-u 100", ncpus / 2, n_iters, time, 64, 2**18),\
  #("perf", "-u 100", ncpus / 1, n_iters, time, 64, 2**18),\

  #increase n
  #("perf", "-u 100 -thread-perc \"80,20;20,80\" -nscale 4 -n 10000000", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 100 -thread-perc \"80,20;20,80\" -nscale 4 -n 100000000", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 100 -thread-perc \"80,20;20,80\" -nscale 4 -n 1000000000", ncpus - 1, n_iters, time, 64, 2**18),\
  #("perf", "-u 100 -thread-perc \"80,20;20,80\" -nscale 4 -n 5000000000", ncpus - 1, n_iters, time, 64, 2**18),\

  ("perf", "-u 100 -n 10000000", ncpus - 1, n_iters, time, 64, 2**18),\
  ("perf", "-u 100 -n 100000000", ncpus - 1, n_iters, time, 64, 2**18),\
  ("perf", "-u 100 -n 1000000000", ncpus - 1, n_iters, time, 64, 2**18),\
]

#configurations for memory benchmark
mem_configs=[\
  ("mem", "-u 100 ", ncpus, mem_n_iters, mem_time, 64, 2**18),\
]


if "-perf" in sys.argv:
  for bench in benchmarks:
    for conf in perf_configs:
      for alloc in allocators:
        bc = bench_conf(alloc, bench, *conf)
        print(bc)
        throughput_mean, memory_max, stddev, throughputs, memory = bc.run()
        print(throughput_mean, memory_max, stddev, throughputs, memory, '\n')

if "-mem" in sys.argv:
  for bench in benchmarks:
    for conf in mem_configs:
      for alloc in allocators:
        for i in range(mem_n_iters):
          bc = bench_conf(alloc, bench, *conf)
          print(bc)
          bc.mstat_run()

print("FINISHED!")
