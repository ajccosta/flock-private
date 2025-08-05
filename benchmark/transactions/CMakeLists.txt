set(SOURCES ../test_sets.cpp)
#set(TPCC_SOURCES test_tpcc.cpp)

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)

copy_bench_file("runtrans")
copy_bench_file("rungraphs")

#set(FLOCK_BENCH "leaftree" "dlist" "btree" "list" "hash_block" "hash_list" "arttree" "skiplist")
set(FLOCK_BENCH "btree" "hash_block" "arttree" "skiplist")

set(YCSB_COMMON_DEFS "HashLock")
#set(TPCC_COMMON_DEFS "NoShortcut;LargeLog") 

find_library(DEQALLOC_LIB deqalloc PATHS ${CMAKE_LIBRARY_PATH})
if(NOT DEQALLOC_LIB)
  message(FATAL_ERROR "Could not find deqalloc.")
endif()

function(add_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE verlib)
  target_link_libraries(${NAME} PRIVATE ${DEQALLOC_LIB})
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

foreach(bench ${FLOCK_BENCH})
  #Use malloc directly
  add_benchmark(${bench}_lck ${STRUCT_DIR}/${bench} "NoHelp;USE_MALLOC")
  add_benchmark(${bench}_lf ${STRUCT_DIR}/${bench} "USE_MALLOC")
endforeach()

## *************************
##    Strings
## *************************
#set(STRING_SOURCES ../test_string.cpp)
#
#function(add_string_benchmark NAME STRUCT_NAME DEFS)
#  add_executable(${NAME} ${STRING_SOURCES})
#  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS} ${DEFS})
#  target_link_libraries(${NAME} PRIVATE verlib)
#  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
#endfunction()
#
#set(STRING_BENCH "btree" "hash_block" "arttree")
