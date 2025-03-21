#!/bin/python3

import matplotlib.pyplot as plt
import numpy as np
import sys
import os

if len(sys.argv) < 2:
    print("""\
          usage: python3 make_graphs.py <file1.memusage> ... <file2.memusage> -- <fileN-1.memusage> <fileN.memusage>
          file1.memusage and file2.memusage will be averaged together.
          The same for fileN-1.memusage and fileN.memusage.\
          """)
    exit(-1)

arrays=[]
file_names=[]
for f in sys.argv[1:] + ["--"]:
    if f == "--":
        #calculate average of all arrays
        mi=min(arr[:,1].size for arr in arrays)
        #truncate each array to be of size mi
        num_samples=len(arrays)
        for i in range(num_samples):
            arrays[i]=arrays[i][:mi]
        avg=np.add.reduce(arrays) / num_samples
        common = os.path.commonprefix(file_names)
        plt.plot(avg[:, 0], avg[:, 1], linewidth=1, label=common)
        arrays=[]
        file_names=[]
        continue
    trc=np.loadtxt(f, dtype=int, skiprows=1)
    #delete third column (kernel memory)
    trc=np.delete(trc, 2, 1)
    #truncate first and last 20 elements
    trc=trc[20:]
    trc=trc[:-20]
    #plt.plot(trc[:, 0], trc[:, 1])#, label=''.join(f.split(".")[0:-1]))
    arrays.append(trc)
    file_names.append(f)

plt.legend(ncol=2,loc="upper center", fontsize="x-small", bbox_to_anchor=(0.5, 1.15))
plt.xlabel("time")
plt.ylabel("memory usage (bytes)")
plt.savefig("memory_usage.png")
plt.clf()