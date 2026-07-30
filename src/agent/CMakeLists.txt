add_executable(pipeline_agent pipeline_agent.cpp)
target_include_directories(
  pipeline_agent PRIVATE
  ${CMAKE_SOURCE_DIR}/algorithm
  ${CMAKE_SOURCE_DIR}/vendor
  ${CMAKE_SOURCE_DIR}/business/include
  ${CMAKE_SOURCE_DIR}/pl_launch)
target_link_libraries(
  pipeline_agent PRIVATE
  pipeline_agent_ipc pipeline_event pipeline_launcher pthread)
target_link_options(pipeline_agent PRIVATE -rdynamic)

install(TARGETS pipeline_agent
        RUNTIME DESTINATION ${ESSDKPL_EXEC_INSTALL_PATH})
