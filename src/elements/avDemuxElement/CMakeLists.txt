add_library(es_plavdemux SHARED avDemuxElement.cpp)
set_target_properties(es_plavdemux PROPERTIES VERSION ${ESSDKPL_VERISION}
                                              SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(
  es_plavdemux
  es_plcore
  yaml-cpp
  avformat
  avcodec
  avutil
  swresample)

install(TARGETS es_plavdemux LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                     NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plavdemux.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plavdemux.so\"
        SYMBOLIC
    )
")
