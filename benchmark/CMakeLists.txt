set(SOURCES test_sets.cpp)

set(BENCH_FILES "runtests.py" "runverlib" "runtrans" "run_sanity_checks.py" "runscalability" "runzipf")

function(copy_bench_file NAME)
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

foreach(file ${BENCH_FILES})
  copy_bench_file(${file})
endforeach()

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)
#set(FLOCK_BENCH "leaftree" "blockleaftree" "arttree" "btree" "dlist" "list" "list_onelock" "hash" "avltree") 
#set(FLOCK_BENCH_USE_CAS "hash_block")
#set(FLOCK_BENCH_RECORDED_ONCE "btree" "list")
set(FLOCK_BENCH "btree" "list" "hash_block" "arttree")
set(FLOCK_BENCH_USE_CAS)
set(FLOCK_BENCH_RECORDED_ONCE)

set(COMMON_DEFS "HashLock") #  "Latency")

function(add_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE verlib)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

foreach(bench ${FLOCK_BENCH_RECORDED_ONCE})
  add_benchmark(${bench}_ver_ro_ls ${STRUCT_DIR}/${bench} "Versioned;Recorded_Once;LazyStamp")
  add_benchmark(${bench}_ver_ro_rs ${STRUCT_DIR}/${bench} "Versioned;Recorded_Once;LazyStamp")
  add_benchmark(${bench}_ver_ro_ls_ss ${STRUCT_DIR}/${bench} "Versioned;Recorded_Once;LazyStamp;SlowStore")
    add_benchmark(${bench}_ver_ro_rs_ss ${STRUCT_DIR}/${bench} "Versioned;Recorded_Once;ReadStamp;SlowStore")
endforeach()

add_benchmark(hash_block_cas ${STRUCT_DIR}/hash_block "NoHelp;UseCAS")

foreach(bench ${FLOCK_BENCH})
  add_benchmark(${bench} ${STRUCT_DIR}/${bench} "NoHelp")
  add_benchmark(${bench}_lf ${STRUCT_DIR}/${bench} "")
  add_benchmark(${bench}_versioned ${STRUCT_DIR}/${bench} "NoHelp;Versioned;HWWriteStamp")
  add_benchmark(${bench}_versioned_lf ${STRUCT_DIR}/${bench} "Versioned;HWWriteStamp")
  add_benchmark(${bench}_validate ${STRUCT_DIR}/${bench} "NoHelp;Validate;Transactional;TransUndo;Versioned;HWWriteStamp")
  add_benchmark(${bench}_validate_lf ${STRUCT_DIR}/${bench} "Validate;Transactional;TransUndo;Versioned;HWWriteStamp")
#  add_benchmark(${bench}_immediate ${STRUCT_DIR}/${bench} "NoHelp;Validate;Immediate;Transactional;TransUndo;Versioned;HWWriteStamp")
#  add_benchmark(${bench}_trans ${STRUCT_DIR}/${bench} "NoHelp;Immediate;Transactional;TransUndo;Versioned;HWWriteStamp")
#  add_benchmark(${bench}_noundo ${STRUCT_DIR}/${bench} "NoHelp;Transactional;Versioned;HWWriteStamp")
#  add_benchmark(${bench}_nohelp ${STRUCT_DIR}/${bench} "NoHelp")
#  add_benchmark(${bench}_indirect_ls ${STRUCT_DIR}/${bench} "Versioned;FullyIndirect;LazyStamp")
#  add_benchmark(${bench}_noshortcut_ls ${STRUCT_DIR}/${bench} "Versioned;Simple;NoShortcut;LazyStamp")
#  add_benchmark(${bench}_ver_rs ${STRUCT_DIR}/${bench} "Versioned;ReadStamp")
#  add_benchmark(${bench}_ver_hw ${STRUCT_DIR}/${bench} "Versioned;HWStamp")
#  add_benchmark(${bench}_ver_ns ${STRUCT_DIR}/${bench} "Versioned;NoIncStamp")
#  add_benchmark(${bench}_ver_ws ${STRUCT_DIR}/${bench} "Versioned;WriteStamp")
#  add_benchmark(${bench}_ver_ls ${STRUCT_DIR}/${bench} "Versioned;LazyStamp")
#  add_benchmark(${bench}_ver_ls_ss ${STRUCT_DIR}/${bench} "Versioned;LazyStamp;SlowStore")
#  add_benchmark(${bench}_ver_nohelp_ls ${STRUCT_DIR}/${bench} "Versioned;NoHelp;LazyStamp")
endforeach()

# None of these need helping
foreach(bench ${FLOCK_BENCH_USE_CAS})
  add_benchmark(${bench}_lf ${STRUCT_DIR}/${bench} "NoHelp;UseCAS")
  add_benchmark(${bench}_lf_nohelp ${STRUCT_DIR}/${bench} "UseCAS")
  add_benchmark(${bench}_lf_indirect_ls ${STRUCT_DIR}/${bench} "NoHelp;UseCAS;Versioned;FullyIndirect;LazyStamp")
  add_benchmark(${bench}_lf_noshortcut_ls ${STRUCT_DIR}/${bench} "NoHelp;UseCAS;Versioned;Simple;NoShortcut;LazyStamp")
  add_benchmark(${bench}_lf_ver_rs ${STRUCT_DIR}/${bench} "NoHelp;UseCAS;Versioned;ReadStamp")
  add_benchmark(${bench}_lf_ver_hw ${STRUCT_DIR}/${bench} "NoHelp;UseCAS;Versioned;HWStamp")
  add_benchmark(${bench}_lf_ver_ns ${STRUCT_DIR}/${bench} "NoHelp;UseCAS;Versioned;NoIncStamp")
  add_benchmark(${bench}_lf_ver_ws ${STRUCT_DIR}/${bench} "NoHelp;UseCAS;Versioned;WriteStamp")
  add_benchmark(${bench}_lf_ver_ls ${STRUCT_DIR}/${bench} "NoHelp;UseCAS;Versioned;LazyStamp")
  add_benchmark(${bench}_lf_ver_nohelp_ls ${STRUCT_DIR}/${bench} "UseCAS;Versioned;LazyStamp")
endforeach()

# *************************
#    Strings
# *************************
set(STRING_SOURCES test_string.cpp)

function(add_string_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${STRING_SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${COMMON_DEFS} ${DEFS})
  target_link_libraries(${NAME} PRIVATE verlib)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

#set(STRING_BENCH "btree" "list" "dlist" "leaftree" "blockleaftree" "arttree" "hash_block" "hash")
set(STRING_BENCH "btree" "hash_block" "arttree")

foreach(bench ${STRING_BENCH})
#  add_string_benchmark(${bench}_string_lf ${STRUCT_DIR}/${bench} "Versioned;HWWriteStamp")
  add_string_benchmark(${bench}_string_validate ${STRUCT_DIR}/${bench} "NoHelp;Validate;Transactional;TransUndo;Versioned;HWWriteStamp")
#  add_string_benchmark(${bench}_string_validate_lf ${STRUCT_DIR}/${bench} "Validate;Transactional;TransUndo;Versioned;HWWriteStamp")
#  add_string_benchmark(${bench}_string_versioned ${STRUCT_DIR}/${bench} "NoHelp;Versioned;HWWriteStamp")
  add_string_benchmark(${bench}_string  ${STRUCT_DIR}/${bench} "NoHelp")
endforeach()

# *************************
#    Other Benchmarks
# *************************

set(OTHER_STRUCT_DIR ${PROJECT_SOURCE_DIR}/otherStructures)

function(add_linked_benchmark NAME LINK)
  add_executable(${NAME} ${SOURCES})
  target_link_libraries(${NAME} PRIVATE verlib ${LINK})
  target_include_directories(${NAME} PRIVATE ${OTHER_STRUCT_DIR}/${NAME})
endfunction()

function(add_linked_string_benchmark NAME LINK)
  add_executable(${NAME}_string ${STRING_SOURCES})
  target_link_libraries(${NAME}_string PRIVATE verlib ${LINK})
  target_include_directories(${NAME}_string PRIVATE ${OTHER_STRUCT_DIR}/${NAME})
endfunction()

find_package(TBB)
add_linked_benchmark(tbbhash TBB::tbb)
add_linked_string_benchmark(tbbhash TBB::tbb)

add_linked_benchmark(sharded_abseil_hash absl::flat_hash_map)
add_linked_benchmark(abseil_unordered absl::flat_hash_map)

set(OTHER_STRUCT_BENCH "harris_list" "harris_list_opt" "libcuckoo" "std_unordered" "std_ordered" "sharded_hash")
set(OTHER_STRUCT_STRING_BENCH "libcuckoo" "std_unordered" "std_ordered" "sharded_hash")

foreach(bench ${OTHER_STRUCT_BENCH})
  add_linked_benchmark(${bench} "")
endforeach()

foreach(bench ${OTHER_STRUCT_STRING_BENCH})
  add_linked_string_benchmark(${bench} "")
endforeach()

