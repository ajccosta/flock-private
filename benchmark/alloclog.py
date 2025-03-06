import sys
import matplotlib.pyplot as plt
import struct
import statistics

read_size = 8

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
    if len(xs) == 0:
        return [], []

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


def build_allocation_timeline_graph(file_name, chunk_step=0.0010, use_range=False):
    trace = load_file(insert_alloc_entry, file_name, read_size)

    alpha=1
     #how much to bundle allocations together (in seconds)

    if use_range:
        range_start=0.6 #ignore beginning of execution
        range_end=1.1
        fig=plt.figure()
        ax=fig.add_subplot(111)
        ax.set_xlim(xmin=range_start, xmax=range_end)

    for k in sorted(trace.keys()):
        xallocs,xfrees,yallocs,yfrees = trace[k]
        new_xallocs, new_yallocs = bundle_allocations(chunk_step, xallocs, yallocs)
        new_xfrees, new_yfrees = bundle_allocations(chunk_step, xfrees, yfrees)
        plt.plot(new_xallocs, new_yallocs, '|', color="blue", alpha=alpha, markersize=4)
        plt.plot(new_xfrees, new_yfrees, '|', color="red", alpha=alpha, markersize=4)
    
    plt.savefig(file_name.split('.')[0] + '_allocation_timeline.png')
    plt.clf()


def insert_alloc_time(trace:dict, t:bytes):
    alloc_t_size = t[0:4]
    ts = get_ts(t[4:])
    ia = is_alloc(alloc_t_size)
    size = get_size(alloc_t_size)

    if "memused" not in trace:
        trace["memused"] = 0
    memused = trace["memused"]

    if ts not in trace:
        trace[ts] = memused

    if(ia):
        memused += size
    else:
        memused -= size
    trace[ts] = memused
    trace["memused"] = memused


def build_memoryusage_timeline_graph(file_name):
    trace = load_file(insert_alloc_time, file_name, read_size)
    if "memused" in trace:
        del trace["memused"]
    xs=trace.keys()
    ys=[]
    for k in trace.keys():
        ys.append(trace[k])

    plt.plot(xs, ys)
    plt.savefig(file_name.split('.')[0] + '_memoryusage_timeline.png')
    plt.clf()


#loads file, processes each entry by callinf <process_f>
def load_file(process_f, file_name, read_size):
    f = open(file_name, 'r+b')
    #read trace file
    trace = {}
    while True:
        d = f.read(read_size)
        if not d:
            break
        process_f(trace, d)
    f.close()
    return trace

file_name = sys.argv[1] if len(sys.argv) > 0 else "alloc.log"

build_allocation_timeline_graph(file_name, chunk_step=0.001)
build_memoryusage_timeline_graph(file_name)