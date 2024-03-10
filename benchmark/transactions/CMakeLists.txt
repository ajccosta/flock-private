set(SOURCES ../test_sets.cpp)
set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)

add_executable(test_tpcc test_tpcc.cpp)
target_link_libraries(test_tpcc PRIVATE verlib)
target_include_directories(test_tpcc PRIVATE ${STRUCT_DIR}/btree)
target_compile_definitions(test_tpcc PRIVATE "NoHelp;Versioned;Validate;Transactional;TransUndo;HWWriteStamp")

copy_bench_file("runtrans")
copy_bench_file("rungraphs")

set(FLOCK_BENCH "leaftree" "dlist" "btree" "list" "hash_block" "hash_list" "arttree")

set(COMMON_DEFS "HashLock") #  "Latency")

function(add_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE verlib)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

add_benchmark(hash_block_cas ${STRUCT_DIR}/hash_block "NoHelp;UseCAS")
#add_benchmark(hash_grow ${STRUCT_DIR}/hash_grow "NoHelp")
#add_benchmark(hash_grow_test ${STRUCT_DIR}/hash_grow_test "NoHelp")

foreach(bench ${FLOCK_BENCH})
  add_benchmark(${bench}_lck ${STRUCT_DIR}/${bench} "NoHelp")
  add_benchmark(${bench}_lf ${STRUCT_DIR}/${bench} "")
  add_benchmark(${bench}_lck_versioned ${STRUCT_DIR}/${bench} "NoHelp;Versioned;HWWriteStamp")
  add_benchmark(${bench}_lf_versioned ${STRUCT_DIR}/${bench} "Versioned;HWWriteStamp")
  add_benchmark(${bench}_lck_validate ${STRUCT_DIR}/${bench} "NoHelp;Validate;Transactional;TransUndo;Versioned;HWWriteStamp")
  add_benchmark(${bench}_lf_validate ${STRUCT_DIR}/${bench} "Validate;Transactional;TransUndo;Versioned;HWWriteStamp")
endforeach()

add_benchmark(tlf_leaftree_lck_validate ${STRUCT_DIR}/tlf_leaftree "NoHelp;Validate;Transactional;TransUndo;Versioned;HWWriteStamp")

# *************************
#    Strings
# *************************
set(STRING_SOURCES ../test_string.cpp)

function(add_string_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${STRING_SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${COMMON_DEFS} ${DEFS})
  target_link_libraries(${NAME} PRIVATE verlib)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

set(STRING_BENCH "btree" "hash_block" "arttree")

foreach(bench ${STRING_BENCH})
  add_string_benchmark(${bench}_string_validate ${STRUCT_DIR}/${bench} "NoHelp;Validate;Transactional;TransUndo;Versioned;HWWriteStamp")
  add_string_benchmark(${bench}_string  ${STRUCT_DIR}/${bench} "NoHelp")
endforeach()

# *************************
#    Other Benchmarks
# *************************

# set(OTHER_STRUCT_DIR ${PROJECT_SOURCE_DIR}/otherStructures)

# function(add_linked_benchmark NAME LINK)
#   add_executable(${NAME} ${SOURCES})
#   target_link_libraries(${NAME} PRIVATE verlib ${LINK})
#   target_include_directories(${NAME} PRIVATE ${OTHER_STRUCT_DIR}/${NAME})
# endfunction()

# function(add_linked_string_benchmark NAME LINK)
#   add_executable(${NAME}_string ${STRING_SOURCES})
#   target_link_libraries(${NAME}_string PRIVATE verlib ${LINK})
#   target_include_directories(${NAME}_string PRIVATE ${OTHER_STRUCT_DIR}/${NAME})
# endfunction()

# find_package(TBB)
# add_linked_benchmark(tbbhash TBB::tbb)
# add_linked_string_benchmark(tbbhash TBB::tbb)

# add_linked_benchmark(sharded_abseil_hash absl::flat_hash_map)
# add_linked_benchmark(abseil_unordered absl::flat_hash_map)

# set(OTHER_STRUCT_BENCH "harris_list" "harris_list_opt" "libcuckoo" "std_unordered" "std_ordered" "sharded_hash")
# set(OTHER_STRUCT_STRING_BENCH "libcuckoo" "std_unordered" "std_ordered" "sharded_hash")

# foreach(bench ${OTHER_STRUCT_BENCH})
#   add_linked_benchmark(${bench} "")
# endforeach()

# foreach(bench ${OTHER_STRUCT_STRING_BENCH})
#   add_linked_string_benchmark(${bench} "")
# endforeach()

