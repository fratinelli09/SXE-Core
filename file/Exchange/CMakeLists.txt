# libExchange.so: exchange interface and simulated exchange
add_library(Exchange SHARED
    Src/CSimExchange.cpp
)

target_include_directories(Exchange PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_link_libraries(Exchange PUBLIC Common)

install(TARGETS Exchange LIBRARY DESTINATION ${SXE_BIN_DIR} RUNTIME DESTINATION ${SXE_BIN_DIR} ARCHIVE DESTINATION ${SXE_BIN_DIR})
