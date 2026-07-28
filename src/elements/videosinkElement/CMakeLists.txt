add_library(es_plvideosink SHARED videosinkElement.cpp)
set_target_properties(es_plvideosink PROPERTIES VERSION ${ESSDKPL_VERISION}
                                                SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plvideosink es_vo es_vps es_plcore)

install(TARGETS es_plvideosink LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                       NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plvideosink.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plvideosink.so\"
        SYMBOLIC
    )
")
