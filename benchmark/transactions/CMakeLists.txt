set(SOURCES ../test_sets.cpp)
#set(TPCC_SOURCES test_tpcc.cpp)

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)

copy_bench_file("runtrans")
copy_bench_file("rungraphs")

#set(FLOCK_BENCH "leaftree" "dlist" "btree" "list" "hash_block" "hash_list" "arttree" "skiplist")
set(FLOCK_BENCH "btree" "hash_block" "arttree" "skiplist")

set(YCSB_COMMON_DEFS "HashLock")
#set(TPCC_COMMON_DEFS "NoShortcut;LargeLog") 

function(add_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE verlib)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

#function(add_tpcc_benchmark NAME STRUCT_NAME DEFS)
#  add_executable(${NAME} ${TPCC_SOURCES})
#  target_compile_definitions(${NAME} PRIVATE ${TPCC_COMMON_DEFS})
#  target_compile_definitions(${NAME} PRIVATE ${DEFS})
#  target_link_libraries(${NAME} PRIVATE verlib)
#  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
#endfunction()

#set(TPCC_BENCH "btree" "hash_block" "arttree" "hash_list" "skiplist")
#
#foreach(bench ${TPCC_BENCH})
#  add_tpcc_benchmark(tpcc_${bench}_lck ${STRUCT_DIR}/${bench} "NoHelp;Transactional;HWWriteStamp")
#  add_tpcc_benchmark(tpcc_${bench}_lck_stm ${STRUCT_DIR}/${bench} "NoHelp;STM;HWWriteStamp")
##  add_tpcc_benchmark(tpcc_${bench}_notrans ${STRUCT_DIR}/${bench} "NoHelp")
##  add_tpcc_benchmark(tpcc_${bench}_versioned ${STRUCT_DIR}/${bench} "NoHelp;Versioned;HWWriteStamp")
#  add_tpcc_benchmark(tpcc_${bench}_lf ${STRUCT_DIR}/${bench} "Transactional;HWWriteStamp")
#  add_tpcc_benchmark(tpcc_${bench}_lf_stm ${STRUCT_DIR}/${bench} "STM;HWWriteStamp")
#endforeach()
#
#add_tpcc_benchmark(tpcc_mix_lck "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "NoHelp;Transactional;HWWriteStamp;Mix")
#add_tpcc_benchmark(tpcc_mix_lf "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Transactional;HWWriteStamp;Mix")
#add_tpcc_benchmark(tpcc_mix_lck_stm "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "NoHelp;STM;HWWriteStamp;Mix")
#add_tpcc_benchmark(tpcc_mix_lf_stm "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "STM;HWWriteStamp;Mix")

#add_benchmark(hash_block_cas ${STRUCT_DIR}/hash_block "NoHelp;UseCAS")
#add_benchmark(hash_grow ${STRUCT_DIR}/hash_grow "NoHelp")
#add_benchmark(hash_grow_test ${STRUCT_DIR}/hash_grow_test "NoHelp")

foreach(bench ${FLOCK_BENCH})
  add_benchmark(${bench}_lck ${STRUCT_DIR}/${bench} "NoHelp")
  add_benchmark(${bench}_lf ${STRUCT_DIR}/${bench} "")
  #add_benchmark(${bench}_lck_versioned ${STRUCT_DIR}/${bench} "NoHelp;Versioned;HWWriteStamp")
  #add_benchmark(${bench}_lf_versioned ${STRUCT_DIR}/${bench} "Versioned;HWWriteStamp")
  #add_benchmark(${bench}_lck_tlf ${STRUCT_DIR}/${bench} "NoHelp;Transactional;HWWriteStamp")
  #add_benchmark(${bench}_lf_tlf ${STRUCT_DIR}/${bench} "Transactional;HWWriteStamp")
  #add_benchmark(${bench}_lck_stm ${STRUCT_DIR}/${bench} "NoHelp;STM;HWWriteStamp")
  #add_benchmark(${bench}_lf_stm ${STRUCT_DIR}/${bench} "STM;HWWriteStamp")

  add_benchmark(${bench}_lck_deqalloc ${STRUCT_DIR}/${bench} "NoHelp;DEQALLOC")
  add_benchmark(${bench}_lf_deqalloc ${STRUCT_DIR}/${bench} "DEQALLOC")
  #add_benchmark(${bench}_lck_versioned_deqalloc ${STRUCT_DIR}/${bench} "NoHelp;Versioned;HWWriteStamp;DEQALLOC")
  #add_benchmark(${bench}_lf_versioned_deqalloc ${STRUCT_DIR}/${bench} "Versioned;HWWriteStamp;DEQALLOC")
  #add_benchmark(${bench}_lck_tlf_deqalloc ${STRUCT_DIR}/${bench} "NoHelp;Transactional;HWWriteStamp;DEQALLOC")
  #add_benchmark(${bench}_lf_tlf_deqalloc ${STRUCT_DIR}/${bench} "Transactional;HWWriteStamp;DEQALLOC")
  #add_benchmark(${bench}_lck_stm_deqalloc ${STRUCT_DIR}/${bench} "NoHelp;STM;HWWriteStamp;DEQALLOC")
  #add_benchmark(${bench}_lf_stm_deqalloc ${STRUCT_DIR}/${bench} "STM;HWWriteStamp;DEQALLOC")

  add_benchmark(${bench}_lck_distack ${STRUCT_DIR}/${bench} "NoHelp;DISTACK")
  add_benchmark(${bench}_lf_distack ${STRUCT_DIR}/${bench} "DISTACK")
  #add_benchmark(${bench}_lck_versioned_distack ${STRUCT_DIR}/${bench} "NoHelp;Versioned;HWWriteStamp;DISTACK")
  #add_benchmark(${bench}_lf_versioned_distack ${STRUCT_DIR}/${bench} "Versioned;HWWriteStamp;DISTACK")
  #add_benchmark(${bench}_lck_tlf_distack ${STRUCT_DIR}/${bench} "NoHelp;Transactional;HWWriteStamp;DISTACK")
  #add_benchmark(${bench}_lf_tlf_distack ${STRUCT_DIR}/${bench} "Transactional;HWWriteStamp;DISTACK")
  #add_benchmark(${bench}_lck_stm_distack ${STRUCT_DIR}/${bench} "NoHelp;STM;HWWriteStamp;DISTACK")
  #add_benchmark(${bench}_lf_stm_distack ${STRUCT_DIR}/${bench} "STM;HWWriteStamp;DISTACK")

  #Use malloc directly
  add_benchmark(${bench}_lck_malloc ${STRUCT_DIR}/${bench} "NoHelp;USE_MALLOC")
  add_benchmark(${bench}_lf_malloc ${STRUCT_DIR}/${bench} "USE_MALLOC")
endforeach()

#add_benchmark(tlf_leaftree_lck_tlf ${STRUCT_DIR}/tlf_leaftree "NoHelp;Transactional;HWWriteStamp")
#
#add_benchmark(array_lck_stm ${STRUCT_DIR}/array "NoHelp;STM;HWWriteStamp")
#
#add_benchmark(stm_arttree_lck ${STRUCT_DIR}/stm_arttree "NoHelp;STM;HWWriteStamp")
#add_benchmark(stm_arttree_lf ${STRUCT_DIR}/stm_arttree "STM;HWWriteStamp")
#
#add_benchmark(stm_skiplist_lck ${STRUCT_DIR}/stm_skiplist "NoHelp;STM;HWWriteStamp")
#add_benchmark(stm_skiplist_lf ${STRUCT_DIR}/stm_skiplist "STM;HWWriteStamp")

# *************************
#    Strings
# *************************
set(STRING_SOURCES ../test_string.cpp)

function(add_string_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${STRING_SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS} ${DEFS})
  target_link_libraries(${NAME} PRIVATE verlib)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

set(STRING_BENCH "btree" "hash_block" "arttree")

#foreach(bench ${STRING_BENCH})
#  add_string_benchmark(${bench}_string_tlf ${STRUCT_DIR}/${bench} "NoHelp;Transactional;HWWriteStamp")
#  add_string_benchmark(${bench}_string  ${STRUCT_DIR}/${bench} "NoHelp")
#endforeach()

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

