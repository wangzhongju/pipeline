add_library(es_pltestsink SHARED testSinkElement.cpp)
set_target_properties(es_pltestsink PROPERTIES VERSION ${ESSDKPL_VERISION}
                                               SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_pltestsink es_plcore yaml-cpp)

install(TARGETS es_pltestsink
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH})
