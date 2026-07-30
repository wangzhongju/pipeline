include_directories(
  ${CMAKE_HOME_DIRECTORY}/elements/ ${CMAKE_HOME_DIRECTORY}/core/include/
  ${CMAKE_HOME_DIRECTORY}/mocks/npu_sdk/include/)

link_directories(${CMAKE_BINARY_DIR}/lib/)

add_library(pipeline_launcher STATIC pl_launch.cpp element_option_parser.cpp)
target_include_directories(pipeline_launcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pipeline_launcher PUBLIC es_plcore pthread dl)

add_executable(espl_launch espl_launch_main.cpp)
add_dependencies(espl_launch es_plcore)
# SET_TARGET_PROPERTIES(espl_launch PROPERTIES COMPILE_FLAGS "-pg")
target_link_libraries(espl_launch pipeline_launcher)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

install(TARGETS espl_launch RUNTIME DESTINATION ${ESSDKPL_EXEC_INSTALL_PATH})
