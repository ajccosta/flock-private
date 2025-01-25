set(BENCH_FILES "runtests.py" "bench.py")

#surppress warning
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        #Clang warnings to surpress
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-stringop-overflow")
endif()

function(copy_bench_file NAME)
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

foreach(file ${BENCH_FILES})
  copy_bench_file(${file})
endforeach()

add_subdirectory(transactions)
#add_subdirectory(verlib)


