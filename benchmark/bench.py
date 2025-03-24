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
filter_out="\nduration: [0-9]+(?:.[0-9]*)?|\nsegfault.*\nAborted"

base_directory=subprocess.getoutput("realpath "+re.sub(os.path.basename(__file__), "", os.path.realpath(__file__))+"/..")+"/"
bin_directory="build/benchmark/transactions/"

docker_base_directory="/home/ubuntu"
docker_bin_directory=".build_mstat/benchmark/transactions/"

allocator_pairs = { \
  #allocator_name: (lib, command suffix)
  "jemalloc":           ("/usr/local/lib/libjemalloc.so",   "_malloc"), \
  "mimalloc":           ("/usr/local/lib/libmimalloc.so",   "_malloc"), \
  "mesh":               ("/usr/local/lib/libmesh.so",       "_malloc"), \
  "parlay":             ("/usr/local/lib/libjemalloc.so",   ""), \
  "distack":            ("/usr/local/lib/libjemalloc.so",   "_distack"), \
  "deqalloc":           ("/usr/local/lib/libjemalloc.so",   "_deqalloc"), \
  "deqalloc-jemalloc":  ("/usr/local/lib/libjemalloc.so",   "_deqalloc"), \
  "deqalloc-mimalloc":  ("/usr/local/lib/libmimalloc.so",   "_deqalloc"), \
  "deqalloc-mesh":      ("/usr/local/lib/libmesh.so",       "_deqalloc"), \
}


class bench_conf():
  allocator=None
  allocator_suffix=None
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
    self.allocator_suffix=allocator_pairs[allocator][1]
    self.allocator_lib=allocator_pairs[allocator][0]
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
    return self.build_cmd()
    #return "Benchmark configuration "+\
    #"{benchmark="+str(self.benchmark)+\
    #",num_threads="+str(self.num_threads)+\
    #",num_rounds="+str(self.num_rounds)+\
    #",time="+str(self.time)+\
    #",extra_arguments="+self.extra_arguments+\
    #",steal_retries="+str(self.steal_retries)+\
    #",list_bytes="+str(self.list_bytes)+\
    #"}"

  def build_cmd(self):
    #environment variables to be passed to docker
    env=""
    env+="--env PARLAY_NUM_THREADS="+str(self.num_threads)+" "
    env+="--env PARLAY_STEAL_RETRIES="+str(self.steal_retries)+" "
    env+="--env PARLAY_LIST_BYTES="+str(self.list_bytes)+" "
    if self.replay: env+="--env PARLAY_REPLAY=1 "
    env+="--env LD_PRELOAD="+self.allocator_lib+" "
    #actuall command
    cmd=""
    cmd+="numactl -i all "
    cmd+=f"/home/ubuntu/{docker_bin_directory}{self.benchmark}{self.allocator_suffix} "
    cmd+="-t "+str(self.time)+" "
    cmd+="-r "+str(self.num_rounds)+" "
    cmd+="-p "+str(self.num_threads)+" "
    if self.clear: cmd+="-clear "
    if self.shuffle: cmd+="-shuffle "
    if self.warmup: cmd+="-warmup "
    if self.replay: cmd+="-test_allocator "
    cmd+=self.extra_arguments
    return cmd, env

  #runs num_rounds times
  def run_once(self):
    container_id=self.docker_prepare()
    cmd, env = self.build_cmd()
    out=subprocess.getoutput(f"docker exec {env} {container_id} {cmd}")
    out=re.sub(filter_out, "", out)
    #get throughputs for each run
    throughputs=[float(line.split(',')[-1]) for line in out.split('\n')]
    memoryusage=[float(line.split(',')[-2].split('=')[1]) for line in out.split('\n')]
    return throughputs, memoryusage

  def run(self):
    throughputs,memoryusage=self.run_once()
    mean=statistics.mean(throughputs)
    stddev= statistics.stdev(throughputs) if self.num_rounds > 1 else 0
    memoryusage_mean=statistics.mean(memoryusage)
    memoryusage_stddev= statistics.stdev(memoryusage) if self.num_rounds > 1 else 0
    return mean, stddev, throughputs, memoryusage_mean, memoryusage_stddev

  def mstat_build_cmd(self):
    cmd="mstat "
    cmd+=f"-o /home/ubuntu/traces/{self.allocator}_{self.benchmark}-{datetime.today().strftime('%d%m%y-%H%M%S')}.memusage "
    cmd+="-freq 120 "
    cmd+=f"-env PARLAY_NUM_THREADS={self.num_threads} "
    cmd+=f"-env PARLAY_STEAL_RETRIES={self.steal_retries} "
    cmd+=f"-env PARLAY_LIST_BYTES={self.list_bytes} "
    cmd+=f"-env LD_PRELOAD={self.allocator_lib} "
    #cmd+=f"-env MALLOC_CONF=\"tcache_ncached_max:1-8388608:1,tcache_max:1\" "
    cmd+=f"/home/ubuntu/{docker_bin_directory}{self.benchmark}{self.allocator_suffix} "
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


n_iters=3
time=15
repetitions=10

benchmarks=[\
  "arttree_lck",\
  #"arttree_lf",\
  #"btree_lck",\
  #"btree_lf",\
  "hash_block_lck",\
  #"hash_block_lf",\
  #"skiplist_lck",\
  #"skiplist_lf",\
]

#allocators=["jemalloc", "mimalloc", "mesh", "parlay", "deqalloc", "distack"]
allocators=["jemalloc", "mimalloc", "mesh", "deqalloc-jemalloc", "deqalloc-mimalloc", "deqalloc-mesh"]

configs=[\
  #("-u 100 -upsert", 64, n_iters, time, 64, 2**18-64),\
  #("-u 100 -upsert", 64, n_iters, time, 64, 2**16-64),\
  #("-u 100 -upsert", 64, n_iters, time, 64, 2**14-64),\
  #("-u 100 -upsert", 64, n_iters, time, 64, 2**12-64),\
  #("-u 100 -upsert", 64, n_iters, time, 64, 2**10-64),\
  #("-u 100 -upsert", 64, n_iters, time, 64, 2**8-64),\
  ("-u 100 -upsert -n 10000000", 64, n_iters, time, 64, 2**8-64),\
]

bcs=[]

for bench in benchmarks:
  for alloc in allocators:
    for conf in configs:
      bcs.append(bench_conf(alloc, bench, *conf))

for bc in bcs:
  if "-perf" in sys.argv:
    print(bc.build_cmd())
    mean, stddev, throughputs, memoryusage_mean, memoryusage_stddev = bc.run()
    print(mean, stddev, memoryusage_mean, memoryusage_stddev ,'\n')

  if "-mem" in sys.argv:
    for i in range(repetitions):
      print(bc.mstat_build_cmd())
      bc.mstat_run()



#TODO add MALLOC_CONF automatically
#n=4096; PARLAY_LIST_BYTES=$((1<<22)) MALLOC_CONF="tcache_ncached_max:1-8388608:$n,tcache_max:$n" LD_PRELOAD=~/.lib/libjemalloc.so numactl -i all ./benchmark/transactions/hash_block_lck_malloc -r 10 -p 64 -upsert -u 100 -clear -t 15