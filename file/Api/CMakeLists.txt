# libSXE.so: C interface of the engine (Include/sxe.h)
add_library(SXEApi SHARED
    Src/sxe.cpp
)
set_target_properties(SXEApi PROPERTIES
    OUTPUT_NAME             SXE
    CXX_VISIBILITY_PRESET   hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_include_directories(SXEApi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)
target_compile_definitions(SXEApi PRIVATE
    SXE_BUILD
    SXE_VERSION_STRING="${PROJECT_VERSION}"
)
target_link_libraries(SXEApi PRIVATE
    Engine
    ${CMAKE_DL_LIBS}
)

install(TARGETS SXEApi LIBRARY DESTINATION ${SXE_BIN_DIR})
