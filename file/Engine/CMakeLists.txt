# libEngine.so: engine (thread), strategy loading, cycle archive
add_library(Engine SHARED
    Src/CStrategyLoader.cpp
    Src/CCycleArchive.cpp
    Src/CSimStateFile.cpp
    Src/CEngine.cpp
)

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_link_libraries(Engine PUBLIC
    Common
    Settings
    DataFeed
    Exchange
    SDK
    ${CMAKE_DL_LIBS}
)

install(TARGETS Engine LIBRARY DESTINATION ${SXE_BIN_DIR} RUNTIME DESTINATION ${SXE_BIN_DIR} ARCHIVE DESTINATION ${SXE_BIN_DIR})
