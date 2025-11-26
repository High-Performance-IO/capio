#####################################
# External projects
#####################################
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
FetchContent_Declare(
        mtcl
        GIT_REPOSITORY https://github.com/ParaGroup/MTCL
        GIT_TAG fbd8aa924b916c6578963af315fdb5f10c3969ad
)
FetchContent_Declare(
        capio_cl
        GIT_REPOSITORY https://github.com/High-Performance-IO/CAPIO-CL.git
        GIT_TAG main
)
FetchContent_MakeAvailable(GoogleTest mtcl capio_cl)

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
add_subdirectory(unit/MemoryFiles)
add_subdirectory(multinode/integration)
add_subdirectory(multinode/backend)
