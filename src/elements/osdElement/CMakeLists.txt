option(WITHOSD_CPU "osd ..." ON)
option(WITHOSD_3D "osd ..." OFF)
option(WITHOSD_2D "osd ..." OFF)

if(WITHOSD_3D)
  add_definitions(-DOSD_HW_WITH_3D)
  include_directories(
    ${DEPENDENCE_STAGING_PATH}/usr/include/freetype2/
    ${DEPENDENCE_STAGING_PATH}/usr/include/harfbuzz/
    ${CMAKE_HOME_DIRECTORY}/../gpu_pvr-1.16/rogue/include/
    ${CMAKE_HOME_DIRECTORY}/../gpu_pvr-1.16/rogue/include/khronos/
    ${CMAKE_HOME_DIRECTORY}/../gpu_pvr-1.16/rogue/include/khronos/EGL/
    ${CMAKE_HOME_DIRECTORY}/../gpu_pvr-1.16/rogue/external/include/
    ${DEPENDENCE_STAGING_PATH}/usr/include/libdrm/
    ${CMAKE_HOME_DIRECTORY}/../gpu_pvr-1.16/rogue/include/public/)
  link_directories(${DEPENDENCE_STAGING_PATH}/lib/
                   ${DEPENDENCE_STAGING_PATH}/usr/lib/)
  add_library(
    es_plosd SHARED osdElement.cpp yaml_parser.cpp ./common/esosd_gpu.cpp
                    ./common/freetype.cpp ./common/gles.cpp)
  target_link_libraries(
    es_plosd
    es_plcore
    yaml-cpp
    freetype
    harfbuzz
    EGL
    GLESv2
    drm
    IMGegl)
elseif(WITHOSD_2D)
  add_definitions(-DOSD_HW_WITH_2D)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    include_directories(${DEPENDENCE_STAGING_PATH}/usr/include/freetype2/
                        ${DEPENDENCE_STAGING_PATH}/usr/include/harfbuzz/)
    link_directories(${DEPENDENCE_STAGING_PATH}/lib/
                     ${DEPENDENCE_STAGING_PATH}/usr/lib/)
  else()
    include_directories(/usr/include/freetype2/ /usr/include/harfbuzz/)
    link_directories(/usr/lib/x86_64-linux-gnu/)
  endif()
  add_library(es_plosd SHARED osdElement.cpp yaml_parser.cpp
                              ./common/esosd_2d.cpp ./common/freetype.cpp)
  target_link_libraries(es_plosd es_plcore yaml-cpp freetype harfbuzz es_vps)
elseif(WITHOSD_CPU)
  add_definitions(-DOSD_HW_WITH_CPU)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    include_directories(
      ${DEPENDENCE_STAGING_PATH}/usr/include/freetype2/
      ${DEPENDENCE_STAGING_PATH}/usr/include/harfbuzz/)
    link_directories(${DEPENDENCE_STAGING_PATH}/lib/
                     ${DEPENDENCE_STAGING_PATH}/usr/lib/)
  else()
    include_directories(/usr/include/freetype2/ /usr/include/harfbuzz/)
    link_directories(/usr/lib/x86_64-linux-gnu/)
  endif()
  add_library(es_plosd SHARED osdElement.cpp yaml_parser.cpp
                              ./common/esosd_cpu.cpp ./common/freetype.cpp)
  set_target_properties(es_plosd PROPERTIES VERSION ${ESSDKPL_VERISION}
                                            SOVERSION ${ESSDKPL_SOVERISION})
  target_link_libraries(es_plosd es_plcore yaml-cpp freetype harfbuzz)

  set(ESPL_DX_ROOT "" CACHE PATH "Optional espl_dx installation root")
  find_path(
    ESPL_DX_INCLUDE_DIR
    NAMES dx/dx.h
    HINTS ${ESPL_DX_ROOT}/include)
  find_library(
    ESPL_DX_LIBRARY
    NAMES espl_dx
    HINTS ${ESPL_DX_ROOT}/lib)
  if(ESPL_DX_INCLUDE_DIR AND ESPL_DX_LIBRARY)
    target_compile_definitions(es_plosd PRIVATE HAVE_ESPL_DX)
    target_include_directories(es_plosd PRIVATE ${ESPL_DX_INCLUDE_DIR})
    target_link_libraries(es_plosd ${ESPL_DX_LIBRARY})
  else()
    message(STATUS "espl_dx not found; OSD frame dump support is disabled")
  endif()
endif()

install(TARGETS es_plosd LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                 NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plosd.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plosd.so\"
        SYMBOLIC
    )
")
