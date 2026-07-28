add_library(es_plmux SHARED muxElement.cpp)
set_target_properties(es_plmux PROPERTIES VERSION ${ESSDKPL_VERISION}
                                          SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plmux es_plcore)

install(TARGETS es_plmux LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                 NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plmux.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plmux.so\"
        SYMBOLIC
    )
")
