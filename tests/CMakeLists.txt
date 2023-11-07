#####################################
# External projects
#####################################
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

#####################################
# CMake module imports
#####################################
include(CTest)
include(Catch)

#####################################
# Targets
#####################################
add_subdirectory(posix)
add_subdirectory(server)
add_subdirectory(syscall)
