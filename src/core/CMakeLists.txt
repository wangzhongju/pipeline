aux_source_directory(./src DIR_LIB_SRCS)
aux_source_directory(./tracer DIR_LIB_SRCS)

add_library(es_plcore SHARED ${DIR_LIB_SRCS})

set_target_properties(es_plcore PROPERTIES VERSION ${ESSDKPL_VERISION}
                                           SOVERSION ${ESSDKPL_SOVERISION})

if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
  target_link_libraries(
    es_plcore
    es_memory
    es_sys
    es_video_utils
    zlog
    atomic
    yaml-cpp
    stdc++fs
    numa
    pthread)
else()
  target_link_libraries(
    es_plcore
    es_sys
    es_video_utils
    zlog
    atomic
    yaml-cpp
    stdc++fs
    pthread)
endif()

install(TARGETS es_plcore LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                  NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plcore.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plcore.so\"
        SYMBOLIC
    )
")
