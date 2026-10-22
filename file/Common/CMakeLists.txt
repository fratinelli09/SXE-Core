# libCommon.so: Logger, ThreadBase, TimeUtils, types shared by engine and strategies
add_library(Common SHARED
    Src/Logger.cpp
    Src/TimeUtils.cpp
)

target_include_directories(Common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

find_package(Threads REQUIRED)
target_link_libraries(Common PUBLIC Threads::Threads)

install(TARGETS Common LIBRARY DESTINATION ${SXE_BIN_DIR})
