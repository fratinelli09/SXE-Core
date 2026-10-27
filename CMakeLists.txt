#==========================================================
# SXE Core - Strategy eXecution Engine
#
#   cmake -S . -B CMakeWork
#   cmake --build CMakeWork
#   cmake --install CMakeWork          -> installs into Target/
#
# or ./comp_All.sh, which runs all three steps (Linux).
# Windows (Visual Studio 2022, from a "x64 Native Tools" prompt):
#   cmake -S . -B CMakeWork -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build CMakeWork
#   ctest --test-dir CMakeWork
#   cmake --install CMakeWork
#==========================================================
cmake_minimum_required(VERSION 3.16)
project(SXE VERSION 1.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -ffp-contract=off: a*b+c is never fused into one instruction (FMA), so the results are
# the same on every processor (MSVC does not fuse either)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -ffp-contract=off)
elseif(MSVC)
    add_compile_options(/W3 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Windows: the DLLs export all their functions like the .so on Linux (libSXE excepted), and
# programs and DLLs are built in one folder so that the programs find the DLLs
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# default install prefix: the project's runtime folder
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/Target" CACHE PATH "Cartella di esecuzione" FORCE)
endif()

# folders inside Target/
set(SXE_BIN_DIR      Bin)
set(SXE_STRATEGY_DIR Bin/Strategies)
set(SXE_CONFIG_DIR   Config)
set(SXE_INCLUDE_DIR  Include)            # sxe.h
set(SXE_CMAKE_DIR    cmake)              # SXEConfig.cmake, for find_package(SXE)

# executable and libraries live in Target/Bin and find each other without LD_LIBRARY_PATH
set(CMAKE_INSTALL_RPATH "$ORIGIN")

include(cmake/SXEStrategy.cmake)

# sources: everything in file/, each part depends only on the ones before it
add_subdirectory(file/Common)
add_subdirectory(file/Settings)
add_subdirectory(file/SDK)
add_subdirectory(file/DataFeed)
add_subdirectory(file/Exchange)
add_subdirectory(file/Engine)
add_subdirectory(file/Api)
add_subdirectory(file/Python)
add_subdirectory(file/App)

# strategies: every folder of file/Strategies with a CMakeLists.txt is built,
# together with its tests if it has a Test subfolder with a CMakeLists.txt
file(GLOB SXE_STRATEGY_DIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/file/Strategies/*")
list(SORT SXE_STRATEGY_DIRS)
set(SXE_STRATEGY_TEST_DIRS "")
foreach(STRATEGY_DIR ${SXE_STRATEGY_DIRS})
    if(IS_DIRECTORY "${STRATEGY_DIR}" AND EXISTS "${STRATEGY_DIR}/CMakeLists.txt")
        add_subdirectory("${STRATEGY_DIR}")
        if(EXISTS "${STRATEGY_DIR}/Test/CMakeLists.txt")
            list(APPEND SXE_STRATEGY_TEST_DIRS "${STRATEGY_DIR}/Test")
        endif()
    endif()
endforeach()

# test (CTest): ./comp_All.sh test
option(SXE_TESTS "Compila i test" ON)
if(SXE_TESTS)
    enable_testing()
    add_subdirectory(file/Tests)
    foreach(TEST_DIR ${SXE_STRATEGY_TEST_DIRS})
        add_subdirectory("${TEST_DIR}")
    endforeach()
endif()

# runtime folders
install(DIRECTORY DESTINATION ${SXE_BIN_DIR})
install(DIRECTORY DESTINATION ${SXE_STRATEGY_DIR})
install(DIRECTORY DESTINATION ${SXE_CONFIG_DIR})
install(DIRECTORY DESTINATION Log)
install(DIRECTORY DESTINATION Backup)
