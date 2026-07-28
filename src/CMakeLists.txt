cmake_minimum_required(VERSION 3.14)
project(pipeline VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ESSDKPL_VERISION 1.0)
set(ESSDKPL_SOVERISION 1.0)

set(PIPELINE_LIB_ROOT /usr/local/lib CACHE PATH "Pipeline target library directory")
set(PIPELINE_EXEC_ROOT /opt/demo/pipeline/bin CACHE PATH "Pipeline target executable directory")
set(ESSDKPL_LIB_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}${PIPELINE_LIB_ROOT}")
set(ESSDKPL_EXEC_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}${PIPELINE_EXEC_ROOT}")

set(PIPELINE_SYSROOT_PREFIX "")
if(CMAKE_SYSROOT)
  set(PIPELINE_SYSROOT_PREFIX "${CMAKE_SYSROOT}")
endif()

set(PIPELINE_MULTIARCH_LIB_DIR "lib")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
  set(PIPELINE_MULTIARCH_LIB_DIR "riscv64-linux-gnu")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  set(PIPELINE_MULTIARCH_LIB_DIR "aarch64-linux-gnu")
elseif(CMAKE_LIBRARY_ARCHITECTURE)
  set(PIPELINE_MULTIARCH_LIB_DIR "${CMAKE_LIBRARY_ARCHITECTURE}")
endif()

set(DEPENDENCE_STAGING_PATH "${PIPELINE_SYSROOT_PREFIX}" CACHE PATH "SDK dependency staging/sysroot path")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
      "Choose the build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

add_compile_definitions(SUPPORT_DMA_HEAP)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
  add_compile_definitions(__RISCV__)
endif()

add_subdirectory(
  ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/zlog
  ${CMAKE_BINARY_DIR}/vendor/zlog)

include_directories(
  ${PIPELINE_SYSROOT_PREFIX}/usr/include/
  ${PIPELINE_SYSROOT_PREFIX}/usr/include/essdk
  ${PIPELINE_SYSROOT_PREFIX}/usr/include/essdk/audio
  ${PIPELINE_SYSROOT_PREFIX}/usr/include/essdk/dsp
  ${PIPELINE_SYSROOT_PREFIX}/usr/include/yaml-cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/core/include)

link_directories(
  ${PIPELINE_SYSROOT_PREFIX}/usr/lib/
  ${PIPELINE_SYSROOT_PREFIX}/usr/lib/${PIPELINE_MULTIARCH_LIB_DIR}
  ${PIPELINE_SYSROOT_PREFIX}/lib/${PIPELINE_MULTIARCH_LIB_DIR}
  ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(core)
add_subdirectory(elements)
add_subdirectory(pl_launch)
