project(TauProfiling)

# LLVM currently (7ish) requires this, so I will too
cmake_minimum_required(VERSION 3.4.3)

set (CMAKE_CXX_STANDARD 14)

find_package(LLVM REQUIRED CONFIG)

# required for `add_llvm_loadable_module`
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

if(NOT DEFINED CLANG_VERSION_MAJOR)
  set(CLANG_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
endif()

if(WIN32 OR CYGWIN)
  set(LLVM_LINK_COMPONENTS Core Support)
endif()

add_subdirectory(lib)
