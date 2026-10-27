#==========================================================
# Runs the SXE Core tests (called by CTest with cmake -P)
#
#   MODE=GOLDEN  SXE CONFIG DIR GOLDEN
#       simulation from scratch; the cycles and the final summary of total_result.txt
#       must be identical to the reference file GOLDEN.
#       With the environment variable SXE_UPDATE_GOLDEN=1 the reference is rewritten.
#   MODE=RESUME  SXE CONFIG_FULL DIR_FULL CONFIG_STOP CONFIG_RESUME DIR_RESUME [STOP_REGEX]
#       uninterrupted run against a run stopped halfway (CONFIG_STOP) and resumed
#       (CONFIG_RESUME): the cycles must be identical. STOP_REGEX: how the stopped
#       run must end (default: stop requested by the strategy)
#   MODE=ORDERS  SXE CONFIG DIR MATCH1..MATCH4 ABSENT RESULT_MATCH
#       simulation from scratch; the order log must contain a line matching each MATCHn and
#       none matching ABSENT, total_result.txt a line matching RESULT_MATCH
#   SXE_LAUNCHER (optional, every mode): program that runs SXE, e.g. the Python interpreter
#       when SXE is a Python script
#       (in the regular expressions the field separator ';' is written '|')
#   MODE=BENCH   SXE CONFIG DIR
#       full simulation, prints the steps per second (never fails because of speed).
#==========================================================
cmake_minimum_required(VERSION 3.16)

# one SXE run with empty log and backup (bClean) or left as they are (resume)
function(run_sxe CONFIG DIR bClean)
    if(bClean)
        file(REMOVE_RECURSE "${DIR}/Log" "${DIR}/Backup")
    endif()
    execute_process(COMMAND ${SXE_LAUNCHER} "${SXE}" "${CONFIG}"
                    RESULT_VARIABLE nResult
                    OUTPUT_VARIABLE strOut
                    ERROR_VARIABLE strErr)
    if(NOT nResult EQUAL 0)
        message(FATAL_ERROR "SXE ${CONFIG} exited with code ${nResult}\n${strOut}\n${strErr}")
    endif()
endfunction()

# lines of a file as a CMake list: ';' (the list separator) becomes '|'
function(read_lines FILE OUT)
    file(READ "${FILE}" strText)
    string(REPLACE ";" "|" strText "${strText}")
    string(REGEX REPLACE "\n$" "" strText "${strText}")
    string(REPLACE "\n" ";" vLines "${strText}")
    set(${OUT} "${vLines}" PARENT_SCOPE)
endfunction()

# cycle rows of total_result.txt (without the '#' notes); bSkipOpen: without the cycle
# still open at the end of the simulation, which exists only in runs from scratch
function(read_cycles DIR bSkipOpen OUT)
    read_lines("${DIR}/Backup/total_result.txt" vLines)
    set(vCycles "")
    foreach(strLine IN LISTS vLines)
        if(strLine MATCHES "^#")
            continue()
        endif()
        if(bSkipOpen AND strLine MATCHES "open at end of simulation")
            continue()
        endif()
        list(APPEND vCycles "${strLine}")
    endforeach()
    set(${OUT} "${vCycles}" PARENT_SCOPE)
endfunction()

# summary of the last run without the timing part ("; N steps in X s ...")
function(read_summary DIR OUT)
    read_lines("${DIR}/Backup/total_result.txt" vLines)
    list(FILTER vLines INCLUDE REGEX "^# Simulation finished")
    list(GET vLines -1 strLast)
    string(REGEX REPLACE "[|] [0-9]+ steps in .*$" "" strLast "${strLast}")
    set(${OUT} "${strLast}" PARENT_SCOPE)
endfunction()

# first different line between two lists, for the error message
function(first_difference vA vB OUT)
    list(LENGTH vA nA)
    list(LENGTH vB nB)
    set(strDiff "number of rows ${nA} against ${nB}")
    math(EXPR nLast "${nA} - 1")
    if(nA GREATER 0)
        foreach(i RANGE ${nLast})
            if(i GREATER_EQUAL nB)
                break()
            endif()
            list(GET vA ${i} strA)
            list(GET vB ${i} strB)
            if(NOT strA STREQUAL strB)
                set(strDiff "row ${i}:\n  expected: ${strA}\n  got: ${strB}")
                break()
            endif()
        endforeach()
    endif()
    set(${OUT} "${strDiff}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "GOLDEN")
    run_sxe("${CONFIG}" "${DIR}" TRUE)
    read_cycles("${DIR}" FALSE vCycles)
    read_summary("${DIR}" strSummary)
    list(APPEND vCycles "${strSummary}")

    if("$ENV{SXE_UPDATE_GOLDEN}" STREQUAL "1")
        list(JOIN vCycles "\n" strText)
        string(REPLACE "|" ";" strText "${strText}")
        file(WRITE "${GOLDEN}" "${strText}\n")
        message(STATUS "Reference updated: ${GOLDEN}")
        return()
    endif()

    if(NOT EXISTS "${GOLDEN}")
        message(FATAL_ERROR "Missing reference: ${GOLDEN} (create it with SXE_UPDATE_GOLDEN=1)")
    endif()
    read_lines("${GOLDEN}" vGolden)
    if(NOT vGolden STREQUAL vCycles)
        first_difference("${vGolden}" "${vCycles}" strDiff)
        message(FATAL_ERROR "Result differs from the reference ${GOLDEN}\n${strDiff}")
    endif()
    list(LENGTH vCycles nCycles)
    message(STATUS "Identical to the reference (${nCycles} rows)")

elseif(MODE STREQUAL "RESUME")
    run_sxe("${CONFIG_FULL}" "${DIR_FULL}" TRUE)
    read_cycles("${DIR_FULL}" TRUE vFull)

    run_sxe("${CONFIG_STOP}" "${DIR_RESUME}" TRUE)
    read_lines("${DIR_RESUME}/Backup/total_result.txt" vStop)
    if(NOT STOP_REGEX)
        set(STOP_REGEX "the strategy asked to stop")
    endif()
    list(FILTER vStop INCLUDE REGEX "${STOP_REGEX}")
    if(NOT vStop)
        message(FATAL_ERROR "The run to interrupt did not end as expected (${STOP_REGEX})")
    endif()
    run_sxe("${CONFIG_RESUME}" "${DIR_RESUME}" FALSE)
    read_cycles("${DIR_RESUME}" TRUE vResumed)

    if(NOT vFull STREQUAL vResumed)
        first_difference("${vFull}" "${vResumed}" strDiff)
        message(FATAL_ERROR "The resumed run differs from the uninterrupted one\n${strDiff}")
    endif()
    list(LENGTH vFull nCycles)
    message(STATUS "Resumed run identical to the uninterrupted one (${nCycles} cycles)")

elseif(MODE STREQUAL "ORDERS")
    run_sxe("${CONFIG}" "${DIR}" TRUE)
    file(GLOB vOrderLogs "${DIR}/Backup/b*/Set_Close_log.txt" "${DIR}/Log/Set_Close_log.txt")
    set(vOrders "")
    foreach(strFile IN LISTS vOrderLogs)
        read_lines("${strFile}" vLines)
        list(APPEND vOrders ${vLines})
    endforeach()
    foreach(strMatch IN ITEMS "${MATCH1}" "${MATCH2}" "${MATCH3}" "${MATCH4}")
        if(strMatch)
            set(vFound ${vOrders})
            list(FILTER vFound INCLUDE REGEX "${strMatch}")
            if(NOT vFound)
                list(JOIN vOrders "\n" strAll)
                message(FATAL_ERROR "No order matches '${strMatch}'\n${strAll}")
            endif()
        endif()
    endforeach()
    if(ABSENT)
        set(vFound ${vOrders})
        list(FILTER vFound INCLUDE REGEX "${ABSENT}")
        if(vFound)
            message(FATAL_ERROR "Unexpected order matching '${ABSENT}': ${vFound}")
        endif()
    endif()
    if(RESULT_MATCH)
        read_lines("${DIR}/Backup/total_result.txt" vResult)
        list(FILTER vResult INCLUDE REGEX "${RESULT_MATCH}")
        if(NOT vResult)
            message(FATAL_ERROR "No line of total_result.txt matches '${RESULT_MATCH}'")
        endif()
    endif()
    message(STATUS "Order log as expected")

elseif(MODE STREQUAL "BENCH")
    run_sxe("${CONFIG}" "${DIR}" TRUE)
    read_lines("${DIR}/Backup/total_result.txt" vLines)
    list(FILTER vLines INCLUDE REGEX "^# Simulation finished")
    list(GET vLines -1 strLast)
    string(REGEX MATCH "[0-9]+ steps in .*$" strSpeed "${strLast}")
    message(STATUS "Speed: ${strSpeed}")

else()
    message(FATAL_ERROR "Unknown MODE: '${MODE}'")
endif()
