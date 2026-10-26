# libSettings.so: INI parser (inih), CReader, CSettings (EngineConfig.ini)
add_library(Settings SHARED
    ini.cpp
    INIReader.cpp
    Settings.cpp
)

target_include_directories(Settings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS Settings LIBRARY DESTINATION ${SXE_BIN_DIR} RUNTIME DESTINATION ${SXE_BIN_DIR} ARCHIVE DESTINATION ${SXE_BIN_DIR})
