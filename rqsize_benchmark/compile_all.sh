
echo "Compiling Java benchmarks..."
cd java/
./compile
cd ..
echo "Finished compiling Java benchmarks"

echo "Compiling C++ benchmarks..."
cd cpp/microbench
make bst.rq_lockfree skiplistlock.rq_bundle citrus.rq_bundle
cd ../../

cd ../../build/benchmark
make btree_ver_ls btree_ver_hw btree_ver_ws btree_ver_rs
echo "Finished compiling C++ benchmarks"
