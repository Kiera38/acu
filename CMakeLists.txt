cmake_minimum_required(VERSION 3.20)
project(acu VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(AUI_VERSION v8.0.0-rc.20)
# Use AUI.Boot
file(
    DOWNLOAD
    https://raw.githubusercontent.com/aui-framework/aui/${AUI_VERSION}/aui.boot.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/aui.boot.cmake
)
include(${CMAKE_CURRENT_BINARY_DIR}/aui.boot.cmake)
set(AUIB_VALID_INSTALLATION_PATHS "C:/Program Files/Microsoft Visual Studio/*")
auib_import(
    LLVM https://github.com/llvm/llvm-project 
    VERSION llvmorg-22.1.4
    LINK STATIC
    CMAKE_ARGS 
        -DLLVM_INCLUDE_EXAMPLES=OFF
        -DLLVM_BUILD_EXAMPLES=OFF
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_BUILD_TESTS=OFF
        -DLLVM_INCLUDE_BENCHMARKS=OFF
        -DLLVM_BUILD_BENCHMARKS=OFF
        -DLLVM_INCLUDE_TOOLS=OFF
        # Force TableGen to be built with optimization. This will significantly improve build time.
        -DLLVM_OPTIMIZED_TABLEGEN=ON
        # Limit the maximum number of concurrent link jobs to 1. This should fix low amount of memory issue for link.
        -DLLVM_PARALLEL_LINK_JOBS=1
        -DLLVM_INSTALL_PACKAGE_DIR:PATH=share/llvm
        -DLLVM_USE_CRT_RELEASE=MT
        -DLLVM_USE_CRT_DEBUG=MTd
        -DLLVM_ENABLE_ZLIB=OFF
    CMAKE_WORKING_DIR llvm
)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Map LLVM components to library names
llvm_map_components_to_libnames(llvm_libs
    core
    support
    native
    mc
    target
    binaryformat
    bitreader
    bitwriter
    executionengine
    orcjit
    ipo
    passes
    scalaropts
    selectiondag
    instcombine
    transformutils
    analysis
)

message(STATUS "LLVM LIBS: ${llvm_libs}")

# Define the executable
add_executable(acu
    src/main.cpp
    src/source.cpp
    src/project.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/nodes_output.cpp

    src/errors.cpp
    src/semanal/types.cpp
    src/semanal/resolve.cpp
    src/semanal/resolver.cpp
    src/semanal/resolve_inst.cpp
    src/semanal/type_analyze.cpp
    src/semanal/type_utils.cpp
    src/semanal/ir_str.cpp
    src/semanal/ir.cpp

    src/refanal/generator.cpp
    src/refanal/ir_str.cpp
    src/refanal/ir.cpp

    src/codegen/generator.cpp
    src/codegen/jit.cpp
)

# Set include directories
target_include_directories(acu SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)
target_include_directories(acu PRIVATE
    src
)

# LLVM Definitions
target_compile_definitions(acu PRIVATE ${LLVM_DEFINITIONS})

target_link_libraries(acu PRIVATE ${llvm_libs})
