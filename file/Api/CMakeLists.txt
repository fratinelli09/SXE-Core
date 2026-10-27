# libSXE.so (Windows: SXE.dll + SXE.lib): C interface of the engine (Include/sxe.h)
add_library(SXEApi SHARED
    Src/sxe.cpp
)
set_target_properties(SXEApi PROPERTIES
    OUTPUT_NAME             SXE
    CXX_VISIBILITY_PRESET   hidden
    VISIBILITY_INLINES_HIDDEN ON
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    PDB_NAME                SXEApi
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

install(TARGETS SXEApi LIBRARY DESTINATION ${SXE_BIN_DIR} RUNTIME DESTINATION ${SXE_BIN_DIR} ARCHIVE DESTINATION ${SXE_BIN_DIR})
install(FILES Include/sxe.h DESTINATION ${SXE_INCLUDE_DIR})

# CMake package: find_package(SXE) + target_link_libraries(<program> PRIVATE SXE::SXE).
# SXEConfig.cmake describes libSXE.so / SXE.dll by itself, so the engine libraries it
# loads stay out of the package
include(CMakePackageConfigHelpers)
set(SXE_LIBRARY_FILE ${CMAKE_SHARED_LIBRARY_PREFIX}SXE${CMAKE_SHARED_LIBRARY_SUFFIX})
set(SXE_IMPLIB_FILE  ${CMAKE_IMPORT_LIBRARY_PREFIX}SXE${CMAKE_IMPORT_LIBRARY_SUFFIX})
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/SXEConfig.cmake.in
                              ${CMAKE_CURRENT_BINARY_DIR}/SXEConfig.cmake
                              INSTALL_DESTINATION ${SXE_CMAKE_DIR}
                              PATH_VARS SXE_BIN_DIR SXE_INCLUDE_DIR)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SXEConfigVersion.cmake
                                 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SXEConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/SXEConfigVersion.cmake
        DESTINATION ${SXE_CMAKE_DIR})
