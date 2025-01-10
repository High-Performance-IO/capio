#####################################
# Target information
#####################################
set(TARGET_NAME capio_server_unit_tests)
set(TARGET_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

#####################################
# Target definition
#####################################
add_executable(${TARGET_NAME} ${TARGET_SOURCES})

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest)

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)