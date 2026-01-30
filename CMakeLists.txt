cmake_minimum_required(VERSION 3.10)
project(TimeSeriesDB VERSION 1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler optimizations
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/Compression.cpp
    src/BloomFilter.cpp
    src/ColumnarStorage.cpp
    src/StockRecord.cpp
    src/BPlusTree.cpp
    src/LRUCache.cpp
    src/TimeSeriesDB.cpp
    src/main.cpp
)

# Create executable
add_executable(tsdb_demo ${SOURCES})

# Link threading library
find_package(Threads REQUIRED)
target_link_libraries(tsdb_demo Threads::Threads)

# Installation
install(TARGETS tsdb_demo DESTINATION bin)

# Print configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
