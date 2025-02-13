import sys
import matplotlib.pyplot as plt
import struct
import statistics


def is_alloc(t:bytes)->bool:
    return not (int.from_bytes(t, sys.byteorder) & 0b1)

def get_size(t:bytes)->int:
    return int.from_bytes(t, sys.byteorder) >> 1

def get_ts(t:bytes)->float:
    return struct.unpack('f', t)[0]

def insert_alloc_entry(trace:dict, t:bytes):
    alloc_t_size = t[0:4]
    ts = get_ts(t[4:])
    ia = is_alloc(alloc_t_size)
    size = get_size(alloc_t_size)
    if size not in trace:
        trace[size] = ([],[],[],[])
    xallocs,xfrees,yallocs,yfrees = trace[size]
    if(ia):
        xallocs.append(ts)
        yallocs.append(str(size)+"-alloc") #interpret size as literal
    else:
        xfrees.append(ts)
        yfrees.append(str(size)+"-free") #interpret size as literal
    return ia, size, ts

def bundle_allocations(chunk_step:float, xs, ys):
    curr_ts = 0
    xs_acc = []
    xs_avg = []
    for xts in xs:
        if xts > curr_ts:
            curr_ts += chunk_step
            new_avg = statistics.mean(xs_acc) if len(xs_acc) > 0 else 0
            xs_avg.append(new_avg)
            xs_acc = []
        xs_acc.append(xts)
    return xs_avg, [ys[0]]*len(xs_avg)


f = open("alloc.log", 'r+b')
read_size=8
chunk_step=0.0001 #how much to bundle allocations together (in seconds)

total, nalloc, nfree = (0, 0, 0)

trace = {}
while True:
    d = f.read(read_size)
    if not d:
        break
    insert_alloc_entry(trace, d)

f.close()


alpha=1

range_start=0.2 #ignore beginning of execution
execution_time=0.1
error=0.05#%

fig=plt.figure()
ax=fig.add_subplot(111)
ax.set_xlim(xmin=range_start, xmax=range_start+(execution_time*(1+error)))



for k in sorted(trace.keys()):
    xallocs,xfrees,yallocs,yfrees = trace[k]
    
    new_xallocs, new_yallocs = bundle_allocations(chunk_step, xallocs, yallocs)
    new_xfrees, new_yfrees = bundle_allocations(chunk_step, xfrees, yfrees)
    plt.plot(new_xallocs, new_yallocs, '|', color="blue", alpha=alpha, markersize=4)
    plt.plot(new_xfrees, new_yfrees, '|', color="red", alpha=alpha, markersize=4)

    #plt.plot(xallocs,yallocs, '|', color="blue", alpha=alpha, markersize=4)
    #plt.plot(xfrees,yfrees, '|', color="red", alpha=alpha, markersize=4)

plt.savefig('foo.png')