#==========================================================
# Strategies built OUTSIDE the SXE Core tree (e.g. private strategies in another folder)
#
#   cmake_minimum_required(VERSION 3.16)
#   project(MyStrategies CXX)
#   include(<SXE folder>/cmake/SXEStrategy.cmake)
#   sxe_add_strategy(NAME My SOURCES CMyStrategy.cpp CONFIG My.ini)
#
# Requires SXE already built (Target/Bin/libCommon.so, libSettings.so; on Windows
# Common.dll/.lib, Settings.dll/.lib): headers
# are taken from file/, libraries from Target/Bin. The strategy is installed
# into the SXE Target like the strategies inside the project.
#==========================================================
if(NOT PROJECT_NAME STREQUAL "SXE")
    get_filename_component(SXE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -ffp-contract=off)
    elseif(MSVC)
        add_compile_options(/W3 /utf-8)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    endif()

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo di build" FORCE)
    endif()
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${SXE_ROOT}/Target" CACHE PATH "Cartella di esecuzione di SXE" FORCE)
    endif()

    set(SXE_BIN_DIR      Bin)
    set(SXE_STRATEGY_DIR Bin/Strategies)
    set(SXE_CONFIG_DIR   Config)

    foreach(SXE_LIB Common Settings)
        if(WIN32)
            set(SXE_LIB_FILE "${SXE_ROOT}/Target/Bin/${SXE_LIB}.dll")
        else()
            set(SXE_LIB_FILE "${SXE_ROOT}/Target/Bin/lib${SXE_LIB}.so")
        endif()
        if(NOT EXISTS "${SXE_LIB_FILE}")
            message(FATAL_ERROR "${SXE_LIB_FILE} not found: build and install SXE first")
        endif()
        add_library(SXE_${SXE_LIB} SHARED IMPORTED)
        set_target_properties(SXE_${SXE_LIB} PROPERTIES IMPORTED_LOCATION "${SXE_LIB_FILE}")
        if(WIN32)
            set_target_properties(SXE_${SXE_LIB} PROPERTIES IMPORTED_IMPLIB "${SXE_ROOT}/Target/Bin/${SXE_LIB}.lib")
        endif()
    endforeach()

    add_library(SDK INTERFACE)
    target_include_directories(SDK INTERFACE
        "${SXE_ROOT}/file/SDK"
        "${SXE_ROOT}/file/Common/Include"
        "${SXE_ROOT}/file/Settings"
    )
    target_link_libraries(SDK INTERFACE SXE_Common SXE_Settings)
endif()

#==========================================================
# sxe_add_strategy: builds a strategy as a library the engine can load
#
#   sxe_add_strategy(NAME Template
#                    SOURCES CTemplateStrategy.cpp
#                    CONFIG  Template.ini)
#
# produces lib<NAME>.so (Windows: lib<NAME>.dll), installs it into Target/Bin/Strategies and copies CONFIG into
# Target/Config only if not already there. Only the SXE_* functions are exported.
#==========================================================
function(sxe_add_strategy)
    cmake_parse_arguments(ARG "" "NAME;CONFIG" "SOURCES" ${ARGN})

    if(NOT ARG_NAME OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sxe_add_strategy: NAME and SOURCES are required")
    endif()

    add_library(${ARG_NAME} MODULE ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE SDK)

    # engine libraries are in Target/Bin, the strategy in Target/Bin/Strategies
    set_target_properties(${ARG_NAME} PROPERTIES
        PREFIX "lib"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        INSTALL_RPATH "$ORIGIN/.."
    )

    install(TARGETS ${ARG_NAME} LIBRARY DESTINATION ${SXE_STRATEGY_DIR})

    if(ARG_CONFIG)
        sxe_install_config(${ARG_CONFIG})
    endif()
endfunction()

#==========================================================
# sxe_install_config: copies an example .ini into Target/Config only if it is not
# already there, so a new build does not overwrite local changes
#==========================================================
function(sxe_install_config CONFIG)
    set(CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG}")
    get_filename_component(CONFIG_NAME ${CONFIG_FILE} NAME)
    install(CODE "
        if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/${SXE_CONFIG_DIR}/${CONFIG_NAME}\")
            file(INSTALL \"${CONFIG_FILE}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${SXE_CONFIG_DIR}\")
        endif()
    ")
endfunction()
