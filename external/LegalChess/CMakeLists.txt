cmake_minimum_required(VERSION 3.10)

#project name and languages
project(LegalChess LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")

# set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_CXX_FLAGS_DEBUG "-g")

#Add include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/inc
)

add_library(LegalChess STATIC
    ${CMAKE_SOURCE_DIR}/src/Board.cpp
    ${CMAKE_SOURCE_DIR}/src/Helper.cpp
    ${CMAKE_SOURCE_DIR}/src/Zobrist.cpp
    ${CMAKE_SOURCE_DIR}/src/MoveManager.cpp
)