add_library(avsync SHARED av_sync.c)
set_target_properties(avsync PROPERTIES VERSION ${ESSDKPL_VERISION}
                                        SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(avsync -lm pthread)
