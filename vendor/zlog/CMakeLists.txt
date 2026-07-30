cmake_minimum_required(VERSION 3.14)

set(ZLOG_SOURCES
  src/buf.c
  src/category.c
  src/category_table.c
  src/conf.c
  src/event.c
  src/format.c
  src/level.c
  src/level_list.c
  src/lockfile.c
  src/mdc.c
  src/record.c
  src/record_table.c
  src/rotater.c
  src/rule.c
  src/spec.c
  src/thread.c
  src/zc_arraylist.c
  src/zc_hashtable.c
  src/zc_profile.c
  src/zc_util.c
  src/zlog.c)

find_package(Threads REQUIRED)

add_library(zlog STATIC ${ZLOG_SOURCES})
set_target_properties(zlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(zlog PRIVATE _DEFAULT_SOURCE)
target_include_directories(zlog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(zlog PUBLIC Threads::Threads)
