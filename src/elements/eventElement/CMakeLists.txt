add_library(es_plevent SHARED eventElement.cpp)
set_target_properties(
  es_plevent PROPERTIES VERSION ${ESSDKPL_VERISION}
                        SOVERSION ${ESSDKPL_SOVERISION})
target_include_directories(
  es_plevent PRIVATE
  ${CMAKE_SOURCE_DIR}/business/include
  ${CMAKE_SOURCE_DIR}/evidence
  ${CMAKE_SOURCE_DIR}/algorithm/event/include
  ${CMAKE_SOURCE_DIR}/algorithm/event/core)
target_link_libraries(
  es_plevent pipeline_event pipeline_agent_ipc pipeline_evidence es_plcore pthread)

install(TARGETS es_plevent
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
        NAMELINK_SKIP)
install(CODE "
  file(CREATE_LINK
      \"libes_plevent.so.${ESSDKPL_SOVERISION}\"
      \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plevent.so\"
      SYMBOLIC
  )
")
