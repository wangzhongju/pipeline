if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
  include_directories(
    ${CMAKE_HOME_DIRECTORY}/../../../output/build/npu_runtime-1.0/eswin/host/runtime/include/
  )
else()
  include_directories(
    ${CMAKE_HOME_DIRECTORY}/../../../../../win2030/libs/npu_sdk/eswin/api
    ${CMAKE_HOME_DIRECTORY}/../../../../../win2030/libs/npu_sdk/eswin/host
    ${CMAKE_HOME_DIRECTORY}/../../../../../win2030/libs/npu_sdk/build/staging/usr/include/essdk/
  )
  link_directories(
    ${CMAKE_HOME_DIRECTORY}/../../../../../win2030/libs/npu_sdk/build/install/x86/host/lib/
  )
endif()

add_library(es_plinfer SHARED inferElement.cpp npuUtils.cpp)
set_target_properties(es_plinfer PROPERTIES VERSION ${ESSDKPL_VERISION}
                                            SOVERSION ${ESSDKPL_SOVERISION})
target_link_libraries(es_plinfer edla_runtime es_dsp_runtime es_hae es_plcore
                      stdc++fs)

install(TARGETS es_plinfer LIBRARY DESTINATION ${ESSDKPL_LIB_INSTALL_PATH}
                                   NAMELINK_SKIP)

install(
  CODE "
    file(CREATE_LINK
        \"libes_plinfer.so.${ESSDKPL_SOVERISION}\"
        \"${ESSDKPL_LIB_INSTALL_PATH}/libes_plinfer.so\"
        SYMBOLIC
    )
")
