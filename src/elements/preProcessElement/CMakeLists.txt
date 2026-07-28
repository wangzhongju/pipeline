add_library(es_plpreprocess SHARED preProcElement.cpp yaml_parser.cpp)
set_target_properties(
  es_plpreprocess PROPERTIES VERSION ${ESSDKPL_VERISION} SOVERSION
                                                         ${ESSDKPL_SOVERISION})
target_link_libraries(es_plpreprocess es_plcore yaml-cpp es_vps)

install(TARGETS es_plpreprocess LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                        NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plpreprocess.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plpreprocess.so\"
        SYMBOLIC
    )
")
