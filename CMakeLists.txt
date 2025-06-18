cmake_minimum_required(VERSION 3.15)
project(ChessStrike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

add_executable(chess_strike src/main.cpp)

# Set compiler flags
target_compile_options(chess_strike PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native>
    $<$<NOT:$<CONFIG:Release>>:-g>
    -Wall -Wextra -Wpedantic
)

find_library(LegalChess_LIB 
    NAMES LegalChess
    PATHS ${CMAKE_SOURCE_DIR}/external/LegalChess/build/
    NO_DEFAULT_PATH
)

if(NOT LegalChess_LIB)
    message("Fatal Error. LegalChess is not found.")
endif()

target_include_directories(chess_strike PRIVATE
    ${CMAKE_SOURCE_DIR}/inc/
    ${CMAKE_SOURCE_DIR}/external/LegalChess/inc
)

# Add Conan-generated CMake modules
find_package(uwebsockets REQUIRED)
find_package(libpqxx REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(jwt-cpp REQUIRED)

# Link dependencies
target_link_libraries(chess_strike PRIVATE
    uwebsockets::uwebsockets
    libpqxx::pqxx
    nlohmann_json::nlohmann_json
    jwt-cpp::jwt-cpp
    ${LegalChess_LIB}
)
