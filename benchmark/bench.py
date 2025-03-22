#!/bin/python3

import subprocess
from datetime import date
import multiprocessing
import statistics
import re
import os
from datetime import datetime


#regex to filter out of results
#Mesh segfaults but seems to run fine until the end
filter_out="\nduration: [0-9]+(?:.[0-9]*)?|\nsegfault.*\nAborted"

base_directory=subprocess.getoutput("realpath "+re.sub(os.path.basename(__file__), "", os.path.realpath(__file__))+"/..")+"/"
bin_directory="build/benchmark/transactions/"
docker_bin_directory=".build_mstat/benchmark/transactions/"

allocator_pairs = { \
  #allocator_name: (lib, command suffix)
  "jemalloc": ("/usr/local/lib/libjemalloc.so", "_malloc"), \
  "mimalloc": ("/usr/local/lib/libmimalloc.so", "_malloc"), \
  "mesh":     ("/usr/local/lib/libmesh.so",     "_malloc"), \
  "parlay":   ("/usr/local/lib/libjemalloc.so", ""), \
  "deqalloc": ("/usr/local/lib/libjemalloc.so", "_deqalloc"), \
  "distack":  ("/usr/local/lib/libjemalloc.so", "_distack"), \
}


class bench_conf():
  allocator=None
  allocator_suffix=None
  allocator_lib=None
  benchmark=None
  benchmark_path=None
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
    self.benchmark_path=\
      subprocess.getoutput("realpath "+base_directory+bin_directory+benchmark+self.allocator_suffix)
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
    cmd=""
    cmd+="PARLAY_NUM_THREADS="+str(self.num_threads)+" "
    cmd+="PARLAY_STEAL_RETRIES="+str(self.steal_retries)+" "
    cmd+="PARLAY_LIST_BYTES="+str(self.list_bytes)+" "
    if self.replay: cmd+="PARLAY_REPLAY=1 "
    cmd+="LD_PRELOAD="+self.allocator_lib+" "
    cmd+="numactl -i all "
    cmd+=self.benchmark_path+" "
    cmd+="-t "+str(self.time)+" "
    cmd+="-r "+str(self.num_rounds)+" "
    cmd+="-p "+str(self.num_threads)+" "
    if self.clear: cmd+="-clear "
    if self.shuffle: cmd+="-shuffle "
    if self.warmup: cmd+="-warmup "
    if self.replay: cmd+="-test_allocator "
    cmd+=self.extra_arguments
    return cmd

  #runs num_rounds times
  def run_once(self):
    out=subprocess.getoutput(self.build_cmd())
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
    cmd+=f"/home/ubuntu/{docker_bin_directory}{self.benchmark}{self.allocator_suffix} "
    cmd+=f"-t {self.time} "
    cmd+=f"-r {self.num_rounds} "
    cmd+=f"-p {self.num_threads} "
    if self.clear: cmd+="-clear "
    cmd+=self.extra_arguments
    return cmd

  def docker_compile(self, container_id):
    #compile through docker if not yet compiled
    if not os.path.isdir(base_directory + ".build_mstat") or \
      len(os.listdir(base_directory + ".build_mstat")) == 0:
      dkr_compile=f"""
        cd {base_directory};
        mkdir -p .build_mstat;
        cd .build_mstat;
        cmake ..;
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

  def mstat_prepare(self):
    self.docker_killall()
    container_id=self.docker_start_container()
    self.docker_compile(container_id)
    return container_id

  def mstat_run(self):
    container_id=self.mstat_prepare()
    subprocess.getoutput(f"docker exec {container_id} {self.mstat_build_cmd()}")
    return


n_iters=1
time=30
repetitions=25

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

allocators=["jemalloc", "mimalloc", "mesh", "parlay", "deqalloc", "distack"]

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
  for i in range(repetitions):
    #print(bc.build_cmd())
    #mean, stddev, throughputs, memoryusage_mean, memoryusage_stddev = bc.run()
    #print(mean, stddev, memoryusage_mean, memoryusage_stddev ,'\n')

    print(bc.mstat_build_cmd())
    bc.mstat_run()


#TODO add MALLOC_CONF automatically
#n=4096; PARLAY_LIST_BYTES=$((1<<22)) MALLOC_CONF="tcache_ncached_max:1-8388608:$n,tcache_max:$n" LD_PRELOAD=~/.lib/libjemalloc.so numactl -i all ./benchmark/transactions/hash_block_lck_malloc -r 10 -p 64 -upsert -u 100 -clear -t 15