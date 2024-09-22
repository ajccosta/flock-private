from datetime import date
import os
import sys

print("starting")

threads = [16, 32, 64, 128, 256]
updates = [0, 5, 10, 20, 50, 100]

size = 10000000
thread = 128
zipfian = 0
update = 5
trans = 16

ds_list_hash = ['FuseHashTable', 'MedleyLfHashTable', 'OneFileHashTable']
ds_list_tree = ['FuseBTree', 'MedleyFraserSkipList', 'OneFileSkipList', ]

tm_list = ['Fuse', 'NBTC', 'OneFile']

preamble = " numactl -i all "

test_only = False

runtime = 2
#repeats = 3
repeats = 1
if test_only:
  runtime = 1

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

thread = detectCPUs()

today = date.today().strftime("%m_%d_%y")
outfile = "results/" + today + "_results.txt"

def runstring(op) :
    if test_only:
        cmd = op
    else :
        cmd = op + " >> " + outfile
    os.system("echo \"" + op + "\"")
    x = os.system(cmd)
    if (x) :
        if (os.WEXITSTATUS(x) == 0) : raise NameError("  aborted: " + op)
        os.system("echo Failed")
        runstring("echo Failed")

def runtest(ds, tm, txn_size, up, sz, zipf, th, suffix):
  cmd = "PARLAY_NUM_THREADS=" + str(th) + preamble + "./bin/release/main" + suffix + " -t " + str(th) + " -i " + str(runtime) + " -R " + ds + " -M " + tm + ":txn=" + str(txn_size) + ":update=" + str(up) + ":size=" + str(sz) + ":zipf=" + str(zipf)
  os.system("echo \"" + cmd + "\" >> " + outfile)
  for i in range(repeats):
    runstring(cmd)

os.system("rm " + outfile)

for ltype in ['_lf'] :
  suffix = ltype + "_btree"
  for i in range(len(ds_list_hash)):
    for th in threads:
      runtest(ds_list_hash[i], tm_list[i], trans, update, size, zipfian, th, suffix)
    for up in updates:
      runtest(ds_list_hash[i], tm_list[i], trans, up, size, zipfian, thread, suffix)
  for tree_ltype in ['_btree', '_skiplist'] :
    suffix = ltype + tree_ltype
    for i in range(len(ds_list_tree)):
      for th in threads:
        runtest(ds_list_tree[i], tm_list[i], trans, update, size, zipfian, th, suffix)
      for up in updates:
        runtest(ds_list_tree[i], tm_list[i], trans, up, size, zipfian, thread, suffix)

for ltype in ['_lck'] :
  suffix = ltype + "_btree"
  for th in threads:
    runtest(ds_list_hash[0], tm_list[0], trans, update, size, zipfian, th, suffix)
  for up in updates:
    runtest(ds_list_hash[0], tm_list[0], trans, up, size, zipfian, thread, suffix)
  for tree_ltype in ['_btree', '_skiplist'] :
    suffix = ltype + tree_ltype
    for th in threads:
      runtest(ds_list_tree[0], tm_list[0], trans, update, size, zipfian, th, suffix)
    for up in updates:
      runtest(ds_list_tree[0], tm_list[0], trans, up, size, zipfian, thread, suffix)


print("finished")
