#!/bin/bash

mkdir -p lib
pushd lib

##compile jemalloc
#git clone https://github.com/jemalloc/jemalloc
#pushd jemalloc
#./autogen.sh --enable-doc=no --enable-static=no --disable-stats
#make -j 8
#popd
#cp $(readlink -f jemalloc/lib/libjemalloc.so) libjemalloc.so
#rm -rf jemalloc

##compile mesh
#git clone https://github.com/plasma-umass/mesh
#pushd mesh
#cmake .
#make -j 8
#make -j 8 #for some reason it only builds on the second attempt
#popd
#cp $(readlink -f mesh/build/lib/libmesh.so) libmesh.so
#rm -rf mesh

##compile mimalloc
#git clone https://github.com/microsoft/mimalloc.git
#pushd mimalloc
#git checkout master
#cmake -B out/release
#cmake --build out/release --parallel 8
#popd
#cp $(readlink -f mimalloc/out/release/libmimalloc.so) libmimalloc.so
#rm -rf mimalloc

##compile hoard
#git clone https://github.com/emeryberger/Hoard
#pushd Hoard/src
#make -j 8
#popd
#cp $(readlink -f Hoard/src/libhoard.so) libhoard.so
#rm -rf Hoard

##compile deqalloc
#git clone https://github.com/ajccosta/deqalloc
#pushd deqalloc/examples/deqalloc
#git checkout master
#mkdir -p build
#pushd build
#cmake ..
#make -j
#popd
#popd
#cp $(readlink -f deqalloc/examples/deqalloc/build/libdeqalloc.so) libdeqalloc.so

##compile scalloc
#git clone https://github.com/ajccosta/scalloc.git
#pushd scalloc
#./tools/make_deps.sh
#./tools/gyp --depth=. scalloc.gyp
#BUILDTYPE=Release make
#popd
#cp $(readlink -f scalloc/out/Release/lib.target/libscalloc.so) libscalloc.so
#rm -rf scalloc

#compile tcmalloc
git clone https://github.com/google/tcmalloc
pushd tcmalloc
ORIG=""
sed -i $ORIG '/linkstatic/d' tcmalloc/BUILD
sed -i $ORIG '/linkstatic/d' tcmalloc/internal/BUILD
sed -i $ORIG '/linkstatic/d' tcmalloc/testing/BUILD
sed -i $ORIG '/linkstatic/d' tcmalloc/variants.bzl
gawk -i inplace '(f && g) {$0="linkshared = True, )"; f=0; g=0} /This library provides tcmalloc always/{f=1} /alwayslink/{g=1} 1' tcmalloc/BUILD
gawk -i inplace 'f{$0="cc_binary("; f=0} /This library provides tcmalloc always/{f=1} 1' tcmalloc/BUILD # Change the line after "This library…" to cc_binary (instead of cc_library)
gawk -i inplace '/alwayslink/ && !f{f=1; next} 1' tcmalloc/BUILD # delete only the first instance of "alwayslink"
bazel build -c opt tcmalloc
popd
cp $(readlink -f tcmalloc/bazel-bin/tcmalloc/libtcmalloc.so) libtcmalloc.so
rm -rf tcmalloc

popd
