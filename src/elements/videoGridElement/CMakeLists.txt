add_library(es_plvideogrid SHARED videoGridElement.cpp yaml_parser.cpp)
set_target_properties(es_plvideogrid PROPERTIES VERSION ${ESSDKPL_VERISION}
                                                SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plvideogrid es_plcore yaml-cpp es_vps)

install(TARGETS es_plvideogrid LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                       NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plvideogrid.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plvideogrid.so\"
        SYMBOLIC
    )
")
