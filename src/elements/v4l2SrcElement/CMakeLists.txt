add_library(es_plv4l2src SHARED v4l2SrcElement.cpp)
set_target_properties(es_plv4l2src PROPERTIES VERSION ${ESSDKPL_VERISION}
                                              SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plv4l2src es_plcore yaml-cpp)
