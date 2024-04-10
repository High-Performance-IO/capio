cmake_minimum_required(VERSION 3.21)

## Variables
set(CXX_1000GENOME_MERGE_STREAMING OFF CACHE BOOL
        "Perform the individuals_merge step using the streaming-aware version of the script.")

## Dependencies

# Find Boost libraries
find_package(Boost REQUIRED COMPONENTS iostreams program_options)
include_directories(${Boost_INCLUDE_DIRS})

# Find libcurl
find_package(CURL REQUIRED)

# Find libarchive
find_package(LibArchive REQUIRED)
include_directories(${LibArchive_INCLUDE_DIRS})

## Targets

# Build download script
add_executable(download download.cpp)
target_compile_features(download PRIVATE cxx_std_17)
target_link_libraries(download ${CURL_LIBRARIES} ${Boost_LIBRARIES})

# Build individuals script
add_executable(individuals individuals.cpp)
target_compile_features(individuals PRIVATE cxx_std_17)
target_link_libraries(individuals ${LibArchive_LIBRARIES} ${Boost_LIBRARIES})

# Build individuals_merge script
if(CXX_1000GENOME_MERGE_STREAMING)
    message("Building individuals_merge_streaming.cpp")
    add_executable(individuals_merge individuals_merge_streaming.cpp)
    target_compile_features(individuals_merge PRIVATE cxx_std_17)
    target_link_libraries(individuals_merge ${LibArchive_LIBRARIES} ${Boost_LIBRARIES})
else()
    add_executable(individuals_merge individuals_merge.cpp)
    target_compile_features(individuals_merge PRIVATE cxx_std_17)
    target_link_libraries(individuals_merge ${LibArchive_LIBRARIES})
endif()

# Build sifting script
add_executable(sifting sifting.cpp)
target_compile_features(sifting PRIVATE cxx_std_17)
target_link_libraries(sifting ${Boost_LIBRARIES})

# Build mutation_overlap script
add_executable(mutation_overlap mutation_overlap.cpp utils.cpp)
target_compile_features(mutation_overlap PRIVATE cxx_std_17)
target_link_libraries(mutation_overlap ${LibArchive_LIBRARIES} ${Boost_LIBRARIES})

# Build frequency script
add_executable(frequency frequency.cpp utils.cpp)
target_compile_features(frequency PRIVATE cxx_std_17)
target_link_libraries(frequency ${LibArchive_LIBRARIES} ${Boost_LIBRARIES})
