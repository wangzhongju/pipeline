add_library(es_plvenc SHARED vencElement.cpp ./common/pl_comm_venc.c
                             ./common/pl_encwrapper.c ./common/pl_option_enc.c)
set_target_properties(es_plvenc PROPERTIES VERSION ${ESSDKPL_VERISION}
                                           SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plvenc yaml-cpp es_plcore es_venc es_sys
                      es_video_utils)

install(TARGETS es_plvenc LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                  NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plvenc.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plvenc.so\"
        SYMBOLIC
    )
")
