add_library(es_plqueue SHARED queueElement.cpp)
set_target_properties(es_plqueue PROPERTIES VERSION ${ESSDKPL_VERISION}
                                            SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plqueue es_plcore)

install(TARGETS es_plqueue LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                   NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plqueue.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plqueue.so\"
        SYMBOLIC
    )
")
