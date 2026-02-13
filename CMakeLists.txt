cmake_minimum_required(VERSION 3.20)
project(acu)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the executable
add_executable(acu
    src/main.cpp
    src/parser/lexer.cpp
)

# Set include directories
target_include_directories(acu PRIVATE
    src
)
