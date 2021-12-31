enable_testing()

include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

add_executable(tests_run src/test.cpp src/main.cpp)

target_link_libraries(tests_run gtest gtest_main)
target_link_libraries(tests_run gmock gmock_main)

llvm_map_components_to_libnames(llvm_libs support core irreader mcjit native)

target_link_libraries(tests_run ${llvm_libs})