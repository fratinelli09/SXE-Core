# A program outside SXE Core that uses it as a CMake package (test cmake_package)
cmake_minimum_required(VERSION 3.16)
project(SxePackageTest LANGUAGES C)

find_package(SXE 1.5 REQUIRED)

add_executable(SxePackageTest main.c)
target_link_libraries(SxePackageTest PRIVATE SXE::SXE)
