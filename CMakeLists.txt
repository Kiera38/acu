cmake_minimum_required(VERSION 3.20)
project(acu)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the executable
add_executable(acu
    src/main.cpp
    src/source.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/nodes_output.cpp

    src/semanal/types.cpp
    src/semanal/resolve.cpp
    src/semanal/type_analyze.cpp
    src/semanal/ir_str.cpp
)

# Set include directories
target_include_directories(acu PRIVATE
    src
)
