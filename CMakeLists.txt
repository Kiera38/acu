cmake_minimum_required(VERSION 3.20)
project(acu)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

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

# Fix LLVM's hardcoded path to diaguids.lib if it's incorrect
if(MSVC)
    set(OLD_DIA_PATH "C:/Program Files/Microsoft Visual Studio/2022/Community/DIA SDK/lib/amd64/diaguids.lib")
    set(NEW_DIA_PATH "C:/Program Files/Microsoft Visual Studio/18/Community/DIA SDK/lib/amd64/diaguids.lib")
    
    # We also need to fix transitive dependencies. Since we can't easily list all LLVM targets,
    # we'll try to fix the ones we know or that are commonly used.
    set(LLVM_TARGETS_TO_FIX ${llvm_libs} LLVMDebugInfoPDB LLVMDebugInfoCodeView LLVMSymbolize)
    
    foreach(tgt ${LLVM_TARGETS_TO_FIX})
        if(TARGET ${tgt})
            get_target_property(libs ${tgt} INTERFACE_LINK_LIBRARIES)
            if(libs)
                string(REPLACE "${OLD_DIA_PATH}" "${NEW_DIA_PATH}" fixed_libs "${libs}")
                set_target_properties(${tgt} PROPERTIES INTERFACE_LINK_LIBRARIES "${fixed_libs}")
            endif()
        endif()
    endforeach()
endif()

message(STATUS "LLVM LIBS: ${llvm_libs}")

# Define the executable
add_executable(acu
    src/main.cpp
    src/source.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/nodes_output.cpp

    src/errors.cpp
    src/semanal/types.cpp
    src/semanal/resolve.cpp
    src/semanal/type_analyze.cpp
    src/semanal/ir_str.cpp

    src/refanal/generator.cpp
    src/refanal/ir_str.cpp
    src/refanal/optimizer.cpp
    src/refanal/specifiers.cpp

    src/codegen/generator.cpp
    src/codegen/jit.cpp
)

# Set include directories
target_include_directories(acu PRIVATE
    src
)

# Link LLVM libraries
if(MSVC)
    target_link_libraries(acu PRIVATE "C:/Program Files/Microsoft Visual Studio/18/Community/DIA SDK/lib/amd64/diaguids.lib")
endif()
target_link_libraries(acu PRIVATE ${llvm_libs})
