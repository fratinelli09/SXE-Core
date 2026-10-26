# libSettings.so: INI parser (inih), CReader, CSettings (EngineConfig.ini)
add_library(Settings SHARED
    ini.cpp
    INIReader.cpp
    Settings.cpp
)

target_include_directories(Settings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# inih reads lines of at most INI_MAX_LINE - 2 characters (default 200): enough room for
# long paths in the INI files
target_compile_definitions(Settings PUBLIC INI_MAX_LINE=4096)

install(TARGETS Settings LIBRARY DESTINATION ${SXE_BIN_DIR} RUNTIME DESTINATION ${SXE_BIN_DIR} ARCHIVE DESTINATION ${SXE_BIN_DIR})
