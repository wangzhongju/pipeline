set(ES_TRACKER_API_ROOT "" CACHE PATH "Optional legacy es_tracker_api installation root")
find_path(
  ES_TRACKER_API_INCLUDE_DIR
  NAMES BoxType.h
  HINTS ${ES_TRACKER_API_ROOT}/include)
find_library(
  ES_TRACKER_API_LIBRARY
  NAMES es_tracker_api
  HINTS ${ES_TRACKER_API_ROOT}/lib)

if(NOT ES_TRACKER_API_INCLUDE_DIR
   OR NOT EXISTS ${ES_TRACKER_API_INCLUDE_DIR}/es_tracker_api.h
   OR NOT EXISTS ${ES_TRACKER_API_INCLUDE_DIR}/impl_EsTracker.hpp
   OR NOT ES_TRACKER_API_LIBRARY)
  message(STATUS "Legacy es_tracker_api not found; trackerElement is disabled")
  return()
endif()

find_package(OpenCV REQUIRED)

add_library(es_pltracker SHARED trackerElement.cpp)
target_include_directories(
  es_pltracker PRIVATE
  ${OpenCV_INCLUDE_DIRS}
  ${ES_TRACKER_API_INCLUDE_DIR})
set_target_properties(es_pltracker PROPERTIES VERSION ${ESSDKPL_VERISION}
                                              SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_pltracker ${ES_TRACKER_API_LIBRARY} pthread es_plcore)

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
