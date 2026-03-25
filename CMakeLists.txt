cmake_minimum_required(VERSION 3.16)
project(FlowSketch LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FLOWLLVM_INCLUDEDIR "${CMAKE_CURRENT_SOURCE_DIR}/.llvm-hdrs/usr/lib/llvm-14/include"
    CACHE PATH "Directory containing llvm/IR/Module.h (from llvm-14-dev package)")

if(NOT EXISTS "${FLOWLLVM_INCLUDEDIR}/llvm/IR/Module.h")
  message(FATAL_ERROR
    "LLVM 14 headers not found at ${FLOWLLVM_INCLUDEDIR}.\n"
    "  mkdir -p \"${CMAKE_CURRENT_SOURCE_DIR}\" && cd \"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
    "  apt-get download llvm-14-dev\n"
    "  dpkg-deb -x llvm-14-dev_*.deb .llvm-hdrs")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/log")

set(_flow_llvm_cflags
  -D_GNU_SOURCE
  -D__STDC_CONSTANT_MACROS
  -D__STDC_FORMAT_MACROS
  -D__STDC_LIMIT_MACROS
  -fno-rtti
)

add_library(FlowSketchPass MODULE pass/FlowSketchPass.cpp)
target_include_directories(FlowSketchPass SYSTEM PRIVATE "${FLOWLLVM_INCLUDEDIR}")
target_include_directories(FlowSketchPass PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(FlowSketchPass PRIVATE ${_flow_llvm_cflags})
if(NOT FLOW_SKETCH_LINK_LLVM_SHARED)
  set(FLOW_SKETCH_LINK_LLVM_SHARED ON CACHE BOOL "Link libLLVM-14.so (recommended)")
endif()
if(FLOW_SKETCH_LINK_LLVM_SHARED)
  target_link_directories(FlowSketchPass PRIVATE /usr/lib/x86_64-linux-gnu)
  target_link_libraries(FlowSketchPass PRIVATE LLVM-14)
else()
  message(FATAL_ERROR "Static linking is intentionally omitted; set up libLLVM or extend CMake.")
endif()

add_library(flowsketch_rt STATIC runtime/flowsketch_rt.c)
target_include_directories(flowsketch_rt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(flowsketch-merge tools/flowsketch_merge.cpp)
target_compile_features(flowsketch-merge PRIVATE cxx_std_17)
