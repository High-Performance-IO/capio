#####################################
# External projects
#####################################
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(GoogleTest)

#####################################
# CMake module imports
#####################################
include(CTest)
include(GoogleTest)

#####################################
# Targets
#####################################
add_subdirectory(unit/posix)
add_subdirectory(unit/server)
add_subdirectory(unit/syscall)
add_subdirectory(integration)
