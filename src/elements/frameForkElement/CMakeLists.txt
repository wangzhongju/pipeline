add_library(es_plframefork SHARED frameForkElement.cpp)
set_target_properties(
  es_plframefork PROPERTIES VERSION ${ESSDKPL_VERISION}
                            SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plframefork es_plcore)

install(TARGETS es_plframefork
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
        NAMELINK_SKIP)
install(CODE "
  file(CREATE_LINK
      \"libes_plframefork.so.${ESSDKPL_SOVERISION}\"
      \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plframefork.so\"
      SYMBOLIC
  )
")
