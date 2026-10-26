# libDataFeed.so: price sources (historical CSV in simulation)
add_library(DataFeed SHARED
    Src/CCsvFeed.cpp
)

target_include_directories(DataFeed PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_link_libraries(DataFeed PUBLIC Common)

install(TARGETS DataFeed LIBRARY DESTINATION ${SXE_BIN_DIR} RUNTIME DESTINATION ${SXE_BIN_DIR} ARCHIVE DESTINATION ${SXE_BIN_DIR})
