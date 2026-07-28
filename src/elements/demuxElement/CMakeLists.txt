add_library(es_pldemux SHARED demuxElement.cpp)
set_target_properties(es_pldemux PROPERTIES VERSION ${ESSDKPL_VERISION}
                                            SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_pldemux es_plcore)

install(TARGETS es_pldemux LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                   NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_pldemux.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_pldemux.so\"
        SYMBOLIC
    )
")
