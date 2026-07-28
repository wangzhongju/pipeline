add_library(es_plfilesink SHARED fileSinkElement.cpp)
set_target_properties(es_plfilesink PROPERTIES VERSION ${ESSDKPL_VERISION}
                                               SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plfilesink yaml-cpp es_plcore -lm -ldl pthread)

install(TARGETS es_plfilesink LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                      NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plfilesink.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plfilesink.so\"
        SYMBOLIC
    )
")
