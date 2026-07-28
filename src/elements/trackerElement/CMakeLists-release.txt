include_directories(/usr/include/opencv4 /usr/include/opencv4/opencv2)

add_library(es_pltracker SHARED trackerElement.cpp)
set_target_properties(es_pltracker PROPERTIES VERSION ${ESSDKPL_VERISION}
                                              SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_pltracker es_tracker_api pthread es_plcore)

install(TARGETS es_pltracker LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                     NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_pltracker.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_pltracker.so\"
        SYMBOLIC
    )
")
