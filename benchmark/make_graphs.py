
import matplotlib
matplotlib.use('Agg')  # Must come before pyplot
import matplotlib.pyplot as plt
import numpy as np
import sys
import os
import subprocess

if len(sys.argv) < 2:
    print("""\
usage: python3 make_graphs.py <trace_folder>
example: python3 make_graphs.py trace
    Where trace/ has two subdirectories deqalloc/ and hoard/, each
    with two other subdirectories arttree_lck/ and btree_lck/.
    Then, this script will produce a two graphs, one for arttree_lck
    and another for btree_lck, where each graph compares deqalloc and
    hoard's memory usage.\
""")
    exit(-1)

def path(*p):
    return os.path.join(*p)

directory=sys.argv[1]
allocators = os.listdir(directory)
results_directory="graphs"
subprocess.getoutput(f"mkdir -p {results_directory}")

all_are_dirs = all(os.path.isdir(os.path.join(directory, entry)) for entry in allocators)
if not all_are_dirs:
    print("Error. Some file inside of", directory, "is not a subdirectory.")
    exit(-1)

graph_d={}

for alloc in allocators:
    benchmarks = os.listdir(path(directory, alloc))
    for bench in benchmarks:
        if bench not in graph_d:
            graph_d[bench] = []
        graph_d[bench].append(alloc)

for graph in graph_d:
    allocs = graph_d[graph]
    bench = graph

    for alloc in allocs:
        memusage_files = os.listdir(path(directory, alloc, bench))
        arrays=[]
        for f in memusage_files:
            trc=np.genfromtxt(path(directory, alloc, bench, f),
                dtype=int,
                skip_header=1,
                skip_footer=1)
            #delete third column (kernel memory)
            if trc.size == 0:
                print("Warning!", path(directory, alloc, bench, f),"is empty. Ignoring it.")
                continue

            trc=np.delete(trc, 2, 1)
            #truncate first and last 20 elements
            #trc=trc[20:]
            #trc=trc[:-20]
            #plt.plot(trc[:, 0], trc[:, 1])
            arrays.append(trc)
        #calculate average of all arrays
        mi=min(arr[:,1].size for arr in arrays)
        #truncate each array to be of size mi
        num_samples=len(arrays)
        for i in range(num_samples):
            arrays[i]=arrays[i][:mi]
        avg=np.add.reduce(arrays) / num_samples
        plt.plot(avg[:, 0], avg[:, 1], linewidth=2, label=alloc)

    plt.legend(ncol=2,loc="upper center", fontsize="x-small", bbox_to_anchor=(0.5, 1.15))
    plt.xlabel("time")
    plt.ylabel("memory usage (bytes)")
    plt.savefig(path(results_directory, bench + "_memory_usage.png"))
    plt.clf()