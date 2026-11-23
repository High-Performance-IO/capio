#####################################
# Target information
#####################################
set(TARGET_NAME capio_fs_streaming_tests)
set(APP_NAME capio_fs_streaming_app)
file(GLOB TARGET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

#####################################
# Target definition
#####################################
# The tests are not preloaded: they start a server and preload the FS variant into the app.
add_executable(${TARGET_NAME} ${TARGET_SOURCES})
add_executable(${APP_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp")

#####################################
# Link libraries
#####################################
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest_main)
target_link_libraries(${APP_NAME} PRIVATE Threads::Threads)
add_dependencies(${TARGET_NAME} ${APP_NAME})

#####################################
# Install rules
#####################################
install(TARGETS ${TARGET_NAME} ${APP_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
