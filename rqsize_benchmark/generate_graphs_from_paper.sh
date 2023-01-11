
# Figures 4g and 4h
# python3 run_java_experiments_rqsize.py 100000 72 72 [8,64,256,1024,8192,65536] graphs/figure4g4h.png 5 5 50G

python3 run_rqsize_experiments.py 100000 5 95 [16,256,4096,65536] graphs/rqsize-100K-5up.png 3 5 50G

python3 run_rqsize_experiments.py 100000 20 80 [16,256,4096,65536] graphs/rqsize-100K-20up.png 3 5 50G

python3 run_rqsize_experiments.py 10000000 5 95 [16,256,4096,65536,1048576] graphs/rqsize-10M-5up.png 3 5 50G

python3 run_rqsize_experiments.py 10000000 20 80 [16,256,4096,65536,1048576] graphs/rqsize-10M-20up.png 3 5 50G