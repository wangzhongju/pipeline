add_library(es_pltestsrc SHARED 
    testSrcElement.cpp
    DataGeneratorUtils.cpp
    DataGeneratorFactory.cpp
    EncodeDataGenerator.cpp
    FaceSelectDataGenerator.cpp
    InferDataGenerator.cpp
    PostprocessDataGenerator.cpp
    TrackerDataGenerator.cpp
    OsdDataGenerator.cpp
    MuxTestDataGenerator.cpp
    SaveDataGenerator.cpp
    CompareFaceDataGenerator.cpp
    VideoGridDataGenerator.cpp
    AdecDataGenerator.cpp
    TTSDataGenerator.cpp
)
set_target_properties(es_pltestsrc PROPERTIES VERSION ${ESSDKPL_VERISION}
                                              SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_pltestsrc es_plcore yaml-cpp)

install(TARGETS es_pltestsrc
        LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH})
