# Strategy SDK: headers only (base class, contexts, export macro).
# A strategy links SDK and also gets Common (types, Logger, TimeUtils) and Settings (CReader)
add_library(SDK INTERFACE)

target_include_directories(SDK INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(SDK INTERFACE
    Common
    Settings
)
