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
filter_out="\n*duration: [0-9]+(?:.[0-9]*)?\n|\n*segfault.*\nAborted|\n*Segmentation.*"

base_directory=subprocess.getoutput("realpath "+re.sub(os.path.basename(__file__), "", os.path.realpath(__file__))+"/..")+"/"
bin_directory="build/benchmark/transactions/"

docker_base_directory="/home/ubuntu"
docker_bin_directory=".build_mstat/benchmark/transactions/"

allocator_pairs = { \
  #allocator_name: shared lib relative path
  "jemalloc":           "lib/libjemalloc.so", \
  "mimalloc":           "lib/libmimalloc.so", \
  "mesh":               "lib/libmesh.so",     \
  "hoard":              "lib/libhoard.so",    \
  "deqalloc":           "lib/libdeqalloc.so"
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
  shuffle=None #whether to shuffle memory in the allocator before run
  warmup=None
  def __init__(self,\
    allocator,\
    benchmark,\
    extra_arguments="",\
    num_threads=multiprocessing.cpu_count(),\
    num_rounds=10,\
    time=0.4,\
    steal_retries=32,\
    list_bytes=(2**18-64),\
    replay=False,\
    clear=True,\
    shuffle=False,\
    warmup=False,\
    ):
    #get fullpath of file
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
    self.shuffle=shuffle
    if(shuffle):
      print("WARNING: shuffling is not recomended, it will likely pre-allocate too much memory.")
    self.warmup=warmup
    
  def __str__(self):
    cmd, env = self.build_cmd()
    return env + cmd

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
    cmd+=self.extra_arguments
    return cmd, env

  #runs num_rounds times
  def run_once(self):
    container_id=self.docker_prepare()
    cmd, env = self.build_cmd()
    out=subprocess.getoutput(f"{env} {cmd}")
    out=re.sub(filter_out, "", out)
    #get throughputs for each run
    throughputs=[float(line.split(',')[-1]) for line in out.split('\n')]
    return throughputs

  def run(self):
    throughputs=self.run_once()
    mean=statistics.mean(throughputs)
    if len(throughputs) > 1:
      stddev = statistics.stdev(throughputs)
    else:
      stddev = 0
    return mean, stddev, throughputs

  def mstat_build_cmd(self):
    cmd="mstat "
    cmd+=f"-o /home/ubuntu/traces/{self.allocator}_{self.benchmark}-{datetime.today().strftime('%d%m%y-%H%M%S')}.memusage "
    cmd+="-freq 120 "
    cmd+=f"-env PARLAY_NUM_THREADS={self.num_threads} "
    cmd+=f"-env PARLAY_STEAL_RETRIES={self.steal_retries} "
    cmd+=f"-env PARLAY_LIST_BYTES={self.list_bytes} "
    cmd+=f"-env LD_PRELOAD={self.allocator_lib} "
    #cmd+=f"-env MALLOC_CONF=\"tcache_ncached_max:1-8388608:1,tcache_max:1\" "
    cmd+=f"/home/ubuntu/{docker_bin_directory}{self.benchmark} "
    cmd+=f"-t {self.time} "
    cmd+=f"-r {self.num_rounds} "
    cmd+=f"-p {self.num_threads} "
    if self.clear: cmd+="-clear "
    cmd+=self.extra_arguments+" "
    cmd+="-no_warmup_round "
    return cmd

  def docker_compile(self, container_id):
    #compile through docker if not yet compiled
    if not os.path.isdir(base_directory + ".build_mstat") or \
      len(os.listdir(base_directory + ".build_mstat")) == 0:
      dkr_compile=f"""
        cd {docker_base_directory};
        mkdir -p .build_mstat;
        cd .build_mstat;
        cmake .. -DDOWNLOAD_PARLAY=True;
        make -j;
      """
      subprocess.getoutput("docker exec " + container_id + dkr_compile)

  def docker_killall(self):
    #kill running docker containers
    subprocess.getoutput("docker kill " + subprocess.getoutput("docker ps -aq"))

  def docker_start_container(self):
    dkr_start=f"""
      docker run -dit --privileged -v {base_directory}:/home/ubuntu -w /home/ubuntu --rm ajcostafct/mstat /bin/bash;
    """
    container_id=subprocess.getoutput(dkr_start)
    return container_id

  def docker_prepare(self):
    self.docker_killall()
    container_id=self.docker_start_container()
    self.docker_compile(container_id)
    return container_id

  def mstat_run(self):
    container_id=self.docker_prepare()
    subprocess.getoutput(f"docker exec {container_id} {self.mstat_build_cmd()}")
    return


n_iters=5
time=25

repetitions=10

benchmarks=[\
  "arttree_lck",\
  #"arttree_lf",\
  "btree_lck",\
  #"btree_lf",\
  "hash_block_lck",\
  #"hash_block_lf",\
  "skiplist_lck",\
  #"skiplist_lf",\
]

allocators=["jemalloc", "mimalloc", "mesh", "hoard", "deqalloc"]

ncpus = detectCPUs()

configs=[\
  #("-u 100 -upsert", ncpus, n_iters, time, 64, 2**18-64),\
  #("-u 100 -upsert", ncpus, n_iters, time, 64, 2**16-64),\
  #("-u 100 -upsert", ncpus, n_iters, time, 64, 2**14-64),\
  #("-u 100 -upsert", ncpus, n_iters, time, 64, 2**12-64),\
  #("-u 100 -upsert", ncpus, n_iters, time, 64, 2**10-64),\
  #("-u 100 -upsert", ncpus, n_iters, time, 64, 2**8-64),\

  #Change write %
  ("-u 100 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 90 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 80 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 70 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 60 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 50 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 40 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 30 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 20 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 10 -upsert", ncpus, n_iters, time, 64, 2**18),\
  ("-u 0 -upsert", ncpus, n_iters, time, 64, 2**18),\

  #Change number of threads
  ("-u 100 -upsert", 1, n_iters, time, 64, 2**18),\
  ("-u 100 -upsert", 2, n_iters, time, 64, 2**18),\
  ("-u 100 -upsert", 4, n_iters, time, 64, 2**18),\
  ("-u 100 -upsert", 8, n_iters, time, 64, 2**18),\
  ("-u 100 -upsert", 18, n_iters, time, 64, 2**18),\
  ("-u 100 -upsert", 36, n_iters, time, 64, 2**18),\
  ("-u 100 -upsert", 72, n_iters, time, 64, 2**18),\
  ("-u 100 -upsert", 144, n_iters, time, 64, 2**18),\

  #("-u 100 -upsert -n 10000000", ncpus, n_iters, time, 64, 2**8-64),\
]

bcs=[]

for alloc in allocators:
  for bench in benchmarks:
    for conf in configs:
      bcs.append(bench_conf(alloc, bench, *conf))

for bc in bcs:
  if "-perf" in sys.argv:
    print(bc)
    mean, stddev, throughputs = bc.run()
    print(mean, stddev, '\n')

  if "-mem" in sys.argv:
    for i in range(repetitions):
      print(bc.mstat_build_cmd())
      bc.mstat_run()



#TODO add MALLOC_CONF automatically
#n=4096; PARLAY_LIST_BYTES=$((1<<22)) MALLOC_CONF="tcache_ncached_max:1-8388608:$n,tcache_max:$n" LD_PRELOAD=~/.lib/libjemalloc.so numactl -i all ./benchmark/transactions/hash_block_lck_malloc -r 10 -p 64 -upsert -u 100 -clear -t 15