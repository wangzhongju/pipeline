add_library(es_plevidencerecorder SHARED evidenceRecorderElement.cpp)
set_target_properties(
  es_plevidencerecorder PROPERTIES VERSION ${ESSDKPL_VERISION}
                                   SOVERSION ${ESSDKPL_SOVERISION})
target_include_directories(
  es_plevidencerecorder PRIVATE
  ${CMAKE_SOURCE_DIR}/evidence
  ${CMAKE_SOURCE_DIR}/core/include)
target_link_libraries(
  es_plevidencerecorder pipeline_evidence es_plcore)

install(TARGETS es_plevidencerecorder
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
        NAMELINK_SKIP)
install(CODE "
  file(CREATE_LINK
      \"libes_plevidencerecorder.so.${ESSDKPL_SOVERISION}\"
      \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plevidencerecorder.so\"
      SYMBOLIC
  )
")
