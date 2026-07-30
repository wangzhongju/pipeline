add_library(es_pltrackerlite SHARED trackerLiteElement.cpp)
set_target_properties(
  es_pltrackerlite PROPERTIES VERSION ${ESSDKPL_VERISION}
                              SOVERSION ${ESSDKPL_SOVERISION})
target_include_directories(
  es_pltrackerlite PRIVATE
  ${CMAKE_SOURCE_DIR}/algorithm/tracker/include)
target_link_libraries(es_pltrackerlite pipeline_tracker es_plcore pthread)

install(TARGETS es_pltrackerlite
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
        NAMELINK_SKIP)
install(CODE "
  file(CREATE_LINK
      \"libes_pltrackerlite.so.${ESSDKPL_SOVERISION}\"
      \"${ESSDKPL_LIB_INSTALL_PATH}/libes_pltrackerlite.so\"
      SYMBOLIC
  )
")
