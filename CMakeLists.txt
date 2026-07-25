cmake_minimum_required(VERSION 3.20)
project(acu VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(AUI_VERSION v8.0.0-rc.20)
# Use AUI.Boot
include(aui.boot.cmake)
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

if(TARGET LLVM)
    set(LLVM_LINK_LIBS LLVM)
    message(STATUS "Using shared LLVM target")
else()
    llvm_map_components_to_libnames(
        LLVM_LINK_LIBS
        core
        support
        analysis
        target
        mc
        bitreader
        passes
        codegen
        asmparser
        asmprinter
        ${LLVM_TARGETS_TO_BUILD}
    )
    message(STATUS "Using static LLVM components: ${LLVM_LINK_LIBS}")
endif()


add_library(acuc STATIC
    src/source.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/nodes_str.cpp

    src/errors.cpp
        src/unicode.h
        src/project.cpp
        src/semanal/resolve.h
        src/semanal/resolve.cpp
        src/semanal/type.h
        src/semanal/type.cpp

        # src/refanal/generator.cpp
# src/refanal/ir_str.cpp
# src/refanal/ir.cpp
#
# src/codegen/generator.cpp
# src/codegen/jit.cpp
)
target_include_directories(acuc PUBLIC src)

# Set include directories
target_include_directories(acuc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

# LLVM Definitions
target_compile_definitions(acuc PRIVATE ${LLVM_DEFINITIONS})

target_link_libraries(acuc PRIVATE ${LLVM_LINK_LIBS})

# Define the executable
add_executable(acu src/main.cpp)
target_link_libraries(acu PRIVATE acuc)

