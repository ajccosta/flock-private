cmake_minimum_required(VERSION 3.13)
project(FLOCK VERSION 1.0 DESCRIPTION "Lock-Free Locks" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Set a default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug Release RelWithDebInfo MinSizeRel)" FORCE)
  message(STATUS "No build type specified. Defaulted to RELEASE.")
  message(STATUS "To specify a build type, add -DCMAKE_BUILD_TYPE=<DEBUG/RELEASE/RELWITHDEBINFO/MINSIZEREL>")
endif(NOT CMAKE_BUILD_TYPE)

message(STATUS "--------------- General configuration -------------")
message(STATUS "CMake Generator:                ${CMAKE_GENERATOR}")
message(STATUS "Compiler:                       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type:                     ${CMAKE_BUILD_TYPE}")
message(STATUS "Architecture:                   ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_CXX_FLAGS:                ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG:          ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE:        ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS          ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "CMAKE_INSTALL_PREFIX:           ${CMAKE_INSTALL_PREFIX}" )
# -------------------------------------------------------------------
#                       Library definition

set(FLOCK_INCLUDE_DIR "${FLOCK_SOURCE_DIR}/include")

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
        if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
                #To be able to use dwcas on non-x86 archs
                #https://large-scale-oltp-team.github.io/foedus_code/group__ARMV8.html
                set(ATOMIC_LIB "-latomic")
        endif()
else()
        set(ATOMIC_LIB "-mcx16")
endif()

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-volatile")
endif()


add_library(flock INTERFACE)
target_include_directories(flock INTERFACE ${FLOCK_INCLUDE_DIR})
target_compile_features(flock INTERFACE cxx_std_17)
target_compile_options(flock INTERFACE ${ATOMIC_LIB} -march=native -g)

add_library(verlib INTERFACE)
target_include_directories(verlib INTERFACE ${FLOCK_INCLUDE_DIR})
target_compile_features(verlib INTERFACE cxx_std_17)
#target_compile_options(verlib INTERFACE -mcx16 -march=native -g -DPARLAY_USE_STD_ALLOC=1 -fsanitize=address)
#target_link_options(verlib INTERFACE -fsanitize=address -static-libasan)
target_compile_options(verlib INTERFACE ${ATOMIC_LIB} -march=native)

# Find threading library
find_package(Threads REQUIRED)


# Link against ParlayLib
#
# If the option -DDOWNLOAD_PARLAY is set to true, then we will pull a fresh
# copy of Parlay and use that. Otherwise, we will look for an installed version
# of Parlay on the system.
#
option(DOWNLOAD_PARLAY "Download and use a local copy of Parlay rather than looking for an installed version")
if(DOWNLOAD_PARLAY)
  message(STATUS "Downloading a local copy of ParlayLib")
  # Download and configure GoogleTest
  include(FetchContent)
  FetchContent_Declare(parlaylib
    GIT_REPOSITORY  https://github.com/cmuparlay/parlaylib.git
    GIT_TAG         master
  )
  FetchContent_GetProperties(parlaylib)
  if(NOT parlaylib_POPULATED)
    FetchContent_Populate(parlaylib)  
    add_subdirectory(${parlaylib_SOURCE_DIR} EXCLUDE_FROM_ALL)
  endif()
  target_link_libraries(flock INTERFACE parlay)
  target_link_libraries(verlib INTERFACE parlay)
else()
  find_package(Parlay)
  if(Parlay_FOUND)
    message(STATUS "Parlay configuration found at " ${Parlay_DIR})
    target_link_libraries(flock INTERFACE Parlay::parlay)
    target_link_libraries(verlib INTERFACE Parlay::parlay)
  else()
    message(FATAL_ERROR "ParlayLib is required but could not be found. Either install it, or set the option -DDOWNLOAD_PARLAY=True to download a local copy of Parlay to use")
  endif()
endif()

## Link against jemalloc
#find_library(JEMALLOC_LIB jemalloc)
#if(NOT JEMALLOC_LIB)
#  message(FATAL_ERROR "Could not find jemalloc.")
#else()
#  message("-- Found jemalloc: ${JEMALLOC_LIB}")
#  target_link_libraries(flock INTERFACE optimized ${JEMALLOC_LIB})
#  target_link_libraries(verlib INTERFACE optimized ${JEMALLOC_LIB})    
#endif()

# -------------------------------------------------------------------
#                       Benchmarks

add_subdirectory(benchmark)

#add_subdirectory(setbench)

# add_subdirectory(abseil-cpp)
