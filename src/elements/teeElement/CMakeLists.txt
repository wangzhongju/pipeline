add_library(es_pltee SHARED teeElement.cpp)
set_target_properties(es_pltee PROPERTIES VERSION ${ESSDKPL_VERISION}
                                          SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_pltee es_plcore)

install(TARGETS es_pltee LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                 NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_pltee.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_pltee.so\"
        SYMBOLIC
    )
")
