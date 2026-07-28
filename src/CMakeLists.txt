cmake_minimum_required(VERSION 3.14)

project(
  MyProject
  VERSION 1.0
  LANGUAGES CXX)

set(ESSDKPL_VERISION 1.0)
set(ESSDKPL_SOVERISION 1.0)

set(ESSDKPL_LIB_INSTALL_PATH /usr/local/lib)
set(ESSDKPL_EXEC_INSTALL_PATH /opt/demo/pipeline/bin)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      "Debug"
      CACHE STRING
            "Choose the build type: Debug, Release, RelWithDebInfo, MinSizeRel"
            FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

enable_language(C)
add_compile_definitions(SUPPORT_DMA_HEAP)
add_definitions(-D __RISCV__)

include_directories(
  /usr/include/
  /usr/include/essdk
  /usr/include/essdk/audio
  /usr/include/essdk/dsp
  /usr/include/yaml-cpp
  /opt/demo/pipeline/src/core/include
  /opt/demo/pipeline/src/tools/dx/include)

link_directories(/usr/lib/ /opt/demo/pipeline/lib/)

add_subdirectory(core)
add_subdirectory(elements)
add_subdirectory(pl_launch)
