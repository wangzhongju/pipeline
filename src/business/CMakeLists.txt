find_package(Protobuf REQUIRED)

set(PIPELINE_AGENT_PROTO_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/media-agent.proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/types.proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/version.proto")

protobuf_generate_cpp(
    PIPELINE_AGENT_PROTO_SRCS
    PIPELINE_AGENT_PROTO_HDRS
    ${PIPELINE_AGENT_PROTO_FILES})

add_library(pipeline_agent_ipc SHARED
    ${PIPELINE_AGENT_PROTO_SRCS}
    ${PIPELINE_AGENT_PROTO_HDRS}
    src/AlarmRelay.cpp
    src/IpcClient.cpp
    src/SocketSender.cpp
    src/MessageMapper.cpp)

set_target_properties(
    pipeline_agent_ipc PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION ${ESSDKPL_VERISION}
    SOVERSION ${ESSDKPL_SOVERISION})

target_include_directories(pipeline_agent_ipc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}"
    ${Protobuf_INCLUDE_DIRS})

target_link_libraries(
    pipeline_agent_ipc PUBLIC ${Protobuf_LIBRARIES} pthread)

install(TARGETS pipeline_agent_ipc
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH})
