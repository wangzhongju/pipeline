add_library(es_plpostprocess SHARED postprocessElement.cpp dspOp.cpp cpuOp.cpp
                                    ../common/dspManager.cpp)
set_target_properties(
  es_plpostprocess PROPERTIES VERSION ${ESSDKPL_VERISION} SOVERSION
                                                          ${ESSDKPL_SOVERISION})
target_link_libraries(es_plpostprocess es_accelerator_kit es_plcore)

install(TARGETS es_plpostprocess LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                         NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plpostprocess.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plpostprocess.so\"
        SYMBOLIC
    )
")
