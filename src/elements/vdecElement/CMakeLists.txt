add_library(es_plvdec SHARED vdecElement.cpp ./common/pl_comm_video.c
                             ./common/pl_option_dec.c)
set_target_properties(es_plvdec PROPERTIES VERSION ${ESSDKPL_VERISION}
                                           SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(
  es_plvdec
  es_plcore
  yaml-cpp
  es_vdec
  avformat
  avcodec
  avutil
  swresample)

install(TARGETS es_plvdec LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                  NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plvdec.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plvdec.so\"
        SYMBOLIC
    )
")
