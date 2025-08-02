#!/bin/bash

mkdir -p lib
pushd lib

#compile jemalloc
git clone https://github.com/jemalloc/jemalloc
pushd jemalloc
./autogen.sh --enable-doc=no --enable-static=no --disable-stats
make -j 8
popd
cp $(readlink -f jemalloc/lib/libjemalloc.so) libjemalloc.so
rm -rf jemalloc

#compile mesh
git clone https://github.com/plasma-umass/mesh
pushd mesh
cmake .
make -j 8
make -j 8 #for some reason it only builds on the second attempt
popd
cp $(readlink -f mesh/build/lib/libmesh.so) libmesh.so
rm -rf mesh

#compile mimalloc
git clone https://github.com/microsoft/mimalloc.git
pushd mimalloc
git checkout master
cmake -B out/release
cmake --build out/release --parallel 8
popd
cp $(readlink -f mimalloc/out/release/libmimalloc.so) libmimalloc.so
rm -rf mimalloc

#compile hoard
git clone https://github.com/emeryberger/Hoard
pushd Hoard/src
make -j 8
popd
cp $(readlink -f Hoard/src/libhoard.so) libhoard.so
rm -rf Hoard

#compile deqalloc
git clone https://github.com/ajccosta/deqalloc
pushd deqalloc/examples/deqalloc
git checkout master
mkdir -p build
pushd build
cmake ..
make -j
popd
popd
cp $(readlink -f deqalloc/examples/deqalloc/build/libdeqalloc.so) libdeqalloc.so

git clone https://github.com/ajccosta/scalloc.git
pushd scalloc
./tools/make_deps.sh
./tools/gyp --depth=. scalloc.gyp
BUILDTYPE=Release make
popd
cp $(readlink -f scalloc/out/Release/lib.target/libscalloc.so) libscalloc.so

popd
