add_library(pipeline_evidence SHARED EvidenceService.cpp)
set_target_properties(
  pipeline_evidence PROPERTIES VERSION ${ESSDKPL_VERISION}
                               SOVERSION ${ESSDKPL_SOVERISION})
target_include_directories(
  pipeline_evidence PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/business/include
  ${CMAKE_SOURCE_DIR}/core/include)
target_link_libraries(
  pipeline_evidence PUBLIC pipeline_agent_ipc
  PRIVATE avcodec avformat avutil swscale es_sys)

install(TARGETS pipeline_evidence
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH})
