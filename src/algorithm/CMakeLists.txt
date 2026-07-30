find_path(PIPELINE_EIGEN_INCLUDE_DIR Eigen/Core PATH_SUFFIXES eigen3 REQUIRED)
find_package(OpenSSL REQUIRED)

set(PIPELINE_ALGORITHM_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor"
    "${CMAKE_CURRENT_SOURCE_DIR}/tracker/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/tracker/bytetrack"
    "${CMAKE_CURRENT_SOURCE_DIR}/event/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/event/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/event/core/event"
    "${CMAKE_CURRENT_SOURCE_DIR}/event/core/factory")

add_library(pipeline_bytetrack STATIC
    tracker/bytetrack/byte_track.cpp
    tracker/bytetrack/dataType.cpp
    tracker/bytetrack/utils.cpp
    tracker/bytetrack/kalman_filter/kalmanfilter.cpp
    tracker/bytetrack/kalman_filter/linear_assignment.cpp
    tracker/bytetrack/kalman_filter/track.cpp
    tracker/bytetrack/munkres_assignment/hungarianoper.cpp
    tracker/bytetrack/munkres_assignment/munkres/munkres.cpp)
set_target_properties(pipeline_bytetrack PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pipeline_bytetrack PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tracker/bytetrack"
    "${PIPELINE_EIGEN_INCLUDE_DIR}")

add_library(pipeline_tracker SHARED
    tracker/src/PipelineTracker.cpp
    common/Config.cpp)
target_compile_definitions(pipeline_tracker PRIVATE
    PIPELINE_ALGORITHM_SOURCE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_include_directories(pipeline_tracker PUBLIC ${PIPELINE_ALGORITHM_INCLUDE_DIRS})
target_link_libraries(pipeline_tracker PRIVATE
    pipeline_bytetrack yaml-cpp dl pthread)

add_library(pipeline_event_core STATIC
    event/core/src/event/crowd_gather_detector.cpp
    event/core/src/event/person_leave_detector.cpp
    event/core/src/event/person_running_detector.cpp
    event/core/src/event/target_detection_detector.cpp
    event/core/src/event/vehicle_parking_detector.cpp
    event/core/src/event/vehicle_reverse_detector.cpp
    event/core/src/factory/event_detector_factory.cpp)
set_target_properties(pipeline_event_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pipeline_event_core PUBLIC ${PIPELINE_ALGORITHM_INCLUDE_DIRS})
target_link_libraries(pipeline_event_core PUBLIC yaml-cpp)

add_library(pipeline_event SHARED
    event/src/PipelineEvent.cpp
    event/src/RequestModelPackage.cpp
    common/Config.cpp
    common/model_package.cpp)
target_compile_definitions(pipeline_event PRIVATE
    PIPELINE_ALGORITHM_SOURCE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_include_directories(pipeline_event PUBLIC ${PIPELINE_ALGORITHM_INCLUDE_DIRS})
target_link_libraries(pipeline_event PRIVATE
    pipeline_event_core yaml-cpp OpenSSL::Crypto dl pthread)

install(TARGETS pipeline_tracker pipeline_event
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH})
install(FILES config/Event.yaml config/EsTracker.yaml
        DESTINATION "${CMAKE_INSTALL_PREFIX}/opt/demo/pipeline/config")
file(COPY config/Event.yaml config/EsTracker.yaml
     DESTINATION "${PROJECT_BINARY_DIR}/config")
