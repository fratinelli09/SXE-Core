#==========================================================
# Test cmake_package (called by CTest with cmake -P): installs SXE Core into DIR/SXE, builds
# the program of this folder with find_package(SXE) in DIR/build and runs it.
#
#   BUILD_DIR    build folder of SXE Core       CONFIG      build type (Release, Debug)
#   DIR          work folder of the test        SOURCE      this folder
#   GENERATOR, PLATFORM, C_COMPILER: the same as SXE Core
#==========================================================
file(REMOVE_RECURSE "${DIR}")

function(run_step strWhat)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE nResult OUTPUT_VARIABLE strOut ERROR_VARIABLE strErr)
    if(NOT nResult EQUAL 0)
        message(FATAL_ERROR "${strWhat} failed (${nResult}):\n${strOut}\n${strErr}")
    endif()
    message(STATUS "${strWhat}: ok")
    set(strOut "${strOut}" PARENT_SCOPE)
endfunction()

run_step("install" ${CMAKE_COMMAND} --install "${BUILD_DIR}" --prefix "${DIR}/SXE" --config "${CONFIG}")
set(PLATFORM_ARG "")
if(PLATFORM)
    set(PLATFORM_ARG -A "${PLATFORM}")
endif()
run_step("configure with find_package(SXE)"
         ${CMAKE_COMMAND} -S "${SOURCE}" -B "${DIR}/build" -G "${GENERATOR}" ${PLATFORM_ARG}
         "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DCMAKE_PREFIX_PATH=${DIR}/SXE")
run_step("build" ${CMAKE_COMMAND} --build "${DIR}/build" --config "${CONFIG}")

# Windows: SXE.dll and the engine DLLs are found through PATH
if(CMAKE_HOST_WIN32)
    file(TO_NATIVE_PATH "${DIR}/SXE/Bin" BIN_DIR)
    set(ENV{PATH} "${BIN_DIR};$ENV{PATH}")
endif()
file(GLOB_RECURSE PROGRAM "${DIR}/build/SxePackageTest" "${DIR}/build/SxePackageTest.exe")
if(NOT PROGRAM)
    message(FATAL_ERROR "SxePackageTest not found in ${DIR}/build")
endif()
list(GET PROGRAM 0 PROGRAM)
run_step("run" "${PROGRAM}")
message(STATUS "${strOut}")
