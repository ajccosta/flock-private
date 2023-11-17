
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

ds_list = ['FuseHashTable', 'MedleyLfHashTable', 'OneFileHashTable', 'FuseBTree', 'MedleyFraserSkipList', 'OneFileSkipList', ]
tm_list = ['Fuse', 'NBTC', 'OneFile', 'Fuse', 'NBTC', 'OneFile']

preamble = " numactl -i all "

test_only = True

runtime = 3
if test_only:
  runtime = 1

outfile = "results/nov16_results.txt"

def runstring(op) :
    if test_only:
        cmd = op
    else :
        cmd = op + " >> " + outfile
    os.system("echo \"" + cmd + "\"")
    x = os.system(cmd)
    if (x) :
        if (os.WEXITSTATUS(x) == 0) : raise NameError("  aborted: " + op)
        os.system("echo Failed")
        runstring("echo Failed")

def runtest(ds, tm, txn_size, up, sz, zipf, th):
  cmd = "PARLAY_NUM_THREADS=" + str(th+3) + preamble + "./bin/release/main -t " + str(th) + " -i " + str(runtime) + " -R " + ds + " -M " + tm + ":txn=" + str(txn_size) + ":update=" + str(up) + ":size=" + str(sz) + ":zipf=" + str(zipf)
  runstring(cmd)


for i in range(len(ds_list)):
  for th in threads:
    runtest(ds_list[i], tm_list[i], trans, update, size, zipfian, th)
  for up in updates:
    runtest(ds_list[i], tm_list[i], trans, up, size, zipfian, thread)

print("finished")