#####################################
# Target information
#####################################
set(TARGET_NAME capio_posix)
set(TARGET_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
set(TARGET_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/libcapio_posix.cpp)

#####################################
# Target definition
#####################################
add_library(${TARGET_NAME} SHARED ${TARGET_SOURCES})

#####################################
# External projects
#####################################
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept)
execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            "${CMAKE_CURRENT_SOURCE_DIR}/syscall_intercept"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept"
)
execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept"
)
set(SYSCALL_INTERCEPT_INCLUDE_FOLDER "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept/include")
set(SYSCALL_INTERCEPT_LIB_FOLDERS
        "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept/lib"
        "${CMAKE_CURRENT_BINARY_DIR}/syscall_intercept/lib64"
)

#####################################
# Definitions
#####################################
target_compile_definitions(${TARGET_NAME} PRIVATE __CAPIO_POSIX)

#####################################
# Include files and directories
#####################################
file(GLOB_RECURSE CAPIO_POSIX_HEADERS "*.hpp")
file(GLOB_RECURSE SYSCALL_INTERCEPT_HEADERS "${SYSCALL_INTERCEPT_INCLUDE_FOLDER}/*.h")
target_sources(${TARGET_NAME} PRIVATE
        "${CAPIO_COMMON_HEADERS}"
        "${CAPIO_POSIX_HEADERS}"
        "${SYSCALL_INTERCEPT_HEADERS}"
)
target_include_directories(${TARGET_NAME} PRIVATE
        ${TARGET_INCLUDE_FOLDER}
        ${SYSCALL_INTERCEPT_INCLUDE_FOLDER}
)

#####################################
# Link libraries
#####################################
target_link_directories(${TARGET_NAME} PRIVATE ${SYSCALL_INTERCEPT_LIB_FOLDERS})
target_link_libraries(${TARGET_NAME} PRIVATE rt syscall_intercept)
