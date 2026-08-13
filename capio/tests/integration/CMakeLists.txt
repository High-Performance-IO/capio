add_executable(capio_multinode_split src/split.cpp)
add_executable(capio_multinode_map src/mapreduce.cpp)
add_executable(capio_multinode_merge src/merge.cpp)

target_link_libraries(capio_multinode_split PRIVATE GTest::gtest)
target_link_libraries(capio_multinode_map PRIVATE GTest::gtest)
target_link_libraries(capio_multinode_merge PRIVATE GTest::gtest)

install(TARGETS capio_multinode_split capio_multinode_map capio_multinode_merge
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
