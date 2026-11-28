#####################################
# Target information
#####################################
set(TARGET_NAME capio_syscall_unit_tests)
file(GLOB TARGET_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

# A library whose constructor starts a thread before libcapio is set up: the constructors of the
# libraries a program links run before the one of a preloaded library
set(EARLY_THREAD_LIBRARY capio_syscall_early_thread)
add_library(${EARLY_THREAD_LIBRARY} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/early_thread/early_thread.cpp")
target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/early_thread")

#####################################
# Link libraries
#####################################
target_link_libraries(${EARLY_THREAD_LIBRARY} PRIVATE pthread)
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest ${EARLY_THREAD_LIBRARY})
# Installed next to the CAPIO libraries
set_target_properties(${TARGET_NAME} PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

#####################################
# Configure tests
#####################################
gtest_discover_tests(${TARGET_NAME}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES ENVIRONMENT LD_PRELOAD=${PROJECT_BINARY_DIR}/src/posix/libcapio_posix.so
)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS ${EARLY_THREAD_LIBRARY}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)