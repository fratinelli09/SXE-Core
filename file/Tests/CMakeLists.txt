#==========================================================
# SXE Core tests (CTest): ./comp_All.sh test  or  ctest --test-dir CMakeWork
#
# Fixed real data in Data/ (BTCUSDT 1h and funding 2023-2024), expected results in Golden/.
# Test outputs go to CMakeWork/file/Tests/<test>: Target is not touched.
# After an intended change to the simulator the references are regenerated with
#   SXE_UPDATE_GOLDEN=1 ctest --test-dir CMakeWork -R golden
#==========================================================

# test strategy and a library with the interface of another version (not installed)
foreach(TEST_LIB TestStrategy OldApiStrategy)
    add_library(${TEST_LIB} MODULE TestStrategy/C${TEST_LIB}.cpp)
    target_link_libraries(${TEST_LIB} PRIVATE SDK)
    set_target_properties(${TEST_LIB} PROPERTIES
        PREFIX "lib"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endforeach()

set(SXE_TEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")
set(SXE_TEST_SCRIPT   ${CMAKE_CURRENT_SOURCE_DIR}/SxeTest.cmake CACHE INTERNAL "")
set(SXE_TEST_GOLDEN   ${CMAKE_CURRENT_SOURCE_DIR}/Golden)
set(T_DATA_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/Data CACHE INTERNAL "")
set(TEMPLATE_DIR      ${CMAKE_BINARY_DIR}/file/Strategies/Template)
set(TEMPLATE_CONFIG   ${CMAKE_SOURCE_DIR}/Target/Config/Template.ini)

#----------------------------------------------------------
# sxe_test_config(<name> LIBRARY .. FOLDER .. STRATEGY_CONFIG .. [DIR ..] [RESUME 0|1]
#                 [INTRABAR n] [MARGIN isolated|crossed] [END_DATE YYYY-MM-DD]
#                 [SLIPPAGE x] [FUNDING_MULT x] [TIERS "limit:rate, ..."] [CAPITAL x] [LIQUIDATION 0|1]
#                 [DATA_FILE file] [LIMIT_FILL through|touch])
# defaults = rules the references were created with: slippage 0.02%, funding x1,
# maintenance margin 0.4% on the whole position, capital 5000
# generates CMakeWork/file/Tests/<name>.ini; DIR = Log and Backup folder (default <name>)
#----------------------------------------------------------
function(sxe_test_config NAME)
    cmake_parse_arguments(ARG "" "LIBRARY;FOLDER;STRATEGY_CONFIG;DIR;RESUME;INTRABAR;MARGIN;END_DATE;SLIPPAGE;FUNDING_MULT;TIERS;CAPITAL;LIQUIDATION;DATA_FILE;LIMIT_FILL" "" ${ARGN})
    set(T_NAME            ${NAME})
    set(T_LIBRARY         ${ARG_LIBRARY})
    set(T_STRATEGY_FOLDER ${ARG_FOLDER})
    set(T_STRATEGY_CONFIG ${ARG_STRATEGY_CONFIG})
    set(T_DIR             ${CMAKE_CURRENT_BINARY_DIR}/${NAME})
    set(T_RESUME          0)
    set(T_INTRABAR        1)
    set(T_MARGIN          isolated)
    set(T_END_DATE        "${ARG_END_DATE}")
    set(T_SLIPPAGE        0.0002)
    set(T_FUNDING_MULT    1)
    set(T_TIERS           "1000000000000000:0.004")
    set(T_CAPITAL         5000)
    set(T_LIQUIDATION     1)
    set(T_DATA_FILE       BTCUSDT_1h_2023_2024.csv)
    set(T_LIMIT_FILL      through)
    foreach(OPT SLIPPAGE FUNDING_MULT TIERS CAPITAL LIQUIDATION DATA_FILE LIMIT_FILL)
        if(DEFINED ARG_${OPT})
            set(T_${OPT} "${ARG_${OPT}}")
        endif()
    endforeach()
    if(ARG_DIR)
        set(T_DIR ${CMAKE_CURRENT_BINARY_DIR}/${ARG_DIR})
    endif()
    if(DEFINED ARG_RESUME)
        set(T_RESUME ${ARG_RESUME})
    endif()
    if(DEFINED ARG_INTRABAR)
        set(T_INTRABAR ${ARG_INTRABAR})
    endif()
    if(ARG_MARGIN)
        set(T_MARGIN ${ARG_MARGIN})
    endif()
    configure_file(${SXE_TEST_SOURCE_DIR}/EngineConfig.test.ini.in ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.ini @ONLY)
endfunction()

# test strategy configurations: leverage, step at which to stop [, limit_dip: % below the entry of a
# limit buy, with a limit close at +tp%; 0 = market orders only]
function(sxe_test_strategy_config NAME LEVERAGE STOP_AT_STEP)
    set(T_LEVERAGE     ${LEVERAGE})
    set(T_STOP_AT_STEP ${STOP_AT_STEP})
    set(T_LIMIT_DIP    0)
    if(ARGC GREATER 3)
        set(T_LIMIT_DIP ${ARGV3})
    endif()
    configure_file(TestStrategy/TestStrategy.ini.in ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.ini @ONLY)
endfunction()

sxe_test_strategy_config(TestStrategy_lev3  3  0)
sxe_test_strategy_config(TestStrategy_lev10 10 0)
sxe_test_strategy_config(TestStrategy_stop  3  123457)
sxe_test_strategy_config(TestStrategy_limit       3 0      3)
sxe_test_strategy_config(TestStrategy_limit_stop  3 123457 3)
sxe_test_strategy_config(TestStrategy_limit_small 1 0      2)

set(TEST_DIR ${CMAKE_CURRENT_BINARY_DIR})

#--- 1. configuration and loading: errors detected ---------------------------
sxe_test_config(loader_not_strategy LIBRARY libCommon.so FOLDER ${CMAKE_BINARY_DIR}/file/Common
                STRATEGY_CONFIG ${TEMPLATE_CONFIG})
add_test(NAME loader_not_strategy COMMAND SXE ${TEST_DIR}/loader_not_strategy.ini)
set_tests_properties(loader_not_strategy PROPERTIES PASS_REGULAR_EXPRESSION "Not an SXE strategy")

sxe_test_config(loader_old_api LIBRARY libOldApiStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEMPLATE_CONFIG})
add_test(NAME loader_old_api COMMAND SXE ${TEST_DIR}/loader_old_api.ini)
set_tests_properties(loader_old_api PROPERTIES PASS_REGULAR_EXPRESSION "interface version")

sxe_test_config(loader_missing LIBRARY libMissing.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEMPLATE_CONFIG})
add_test(NAME loader_missing COMMAND SXE ${TEST_DIR}/loader_missing.ini)
set_tests_properties(loader_missing PROPERTIES PASS_REGULAR_EXPRESSION "libMissing.so not found")

# the example EngineConfig.ini must be readable and contain valid values
# (data files and strategy may be missing: only the configuration matters here)
add_test(NAME config_example COMMAND SXE ${CMAKE_SOURCE_DIR}/Target/Config/EngineConfig.ini)
set_tests_properties(config_example PROPERTIES
    PASS_REGULAR_EXPRESSION "SXE START|ERROR|SXE:"
    FAIL_REGULAR_EXPRESSION "Invalid line|is not valid|Required key|out of range|tier '|no tier|invalid date|must be 0 or 1")

#--- 2. results identical to the references ---------------------------------
# Template, path following the candle direction
sxe_test_config(golden_template LIBRARY libTemplate.so FOLDER ${TEMPLATE_DIR} STRATEGY_CONFIG ${TEMPLATE_CONFIG})
# test strategy with leverage 10: orders inside the candle, funding, isolated liquidations
sxe_test_config(golden_isolated LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev10.ini)
# same strategy in crossed mode: the simulation ends at the first liquidation
sxe_test_config(golden_crossed LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev10.ini MARGIN crossed)
# extreme paths inside the candle
sxe_test_config(golden_low_first LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev3.ini INTRABAR 2)
sxe_test_config(golden_high_first LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev3.ini INTRABAR 3)

# real Bitget rules: tiered maintenance margin, slippage 0.005%, funding x2;
# capital 100,000 with leverage 10: the position exceeds 200,000 USDT, where the tiers matter
sxe_test_config(golden_bitget LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev10.ini CAPITAL 100000 SLIPPAGE 0.00005 FUNDING_MULT 2
                TIERS "200000:0.004, 1000000:0.005, 5000000:0.007, 15000000:0.01, 50000000:0.02")

# liquidation off: the engine warns that the result is not realistic when the equity goes below zero
sxe_test_config(no_liquidation_warning LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev10.ini LIQUIDATION 0)
add_test(NAME no_liquidation_warning COMMAND SXE ${TEST_DIR}/no_liquidation_warning.ini)
set_tests_properties(no_liquidation_warning PROPERTIES PASS_REGULAR_EXPRESSION "Equity below zero .* = 0: on a real account")

# limit orders: entry at market, limit buy 3% lower, limit close at +2% moved when the average price changes
sxe_test_config(golden_limit LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_limit.ini)

foreach(GOLDEN_TEST golden_template golden_isolated golden_crossed golden_low_first golden_high_first golden_bitget golden_limit)
    add_test(NAME ${GOLDEN_TEST}
             COMMAND ${CMAKE_COMMAND} -DMODE=GOLDEN -DSXE=$<TARGET_FILE:SXE>
                     -DCONFIG=${TEST_DIR}/${GOLDEN_TEST}.ini -DDIR=${TEST_DIR}/${GOLDEN_TEST}
                     -DGOLDEN=${SXE_TEST_GOLDEN}/${GOLDEN_TEST}.txt -P ${SXE_TEST_SCRIPT})
endforeach()

#--- 3. resume: stop mid-candle + resume = uninterrupted run -----------------
sxe_test_config(resume_full LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev3.ini RESUME 1)
sxe_test_config(resume_stop LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_stop.ini DIR resume_split RESUME 1)
sxe_test_config(resume_continue LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev3.ini DIR resume_split RESUME 1)
add_test(NAME resume
         COMMAND ${CMAKE_COMMAND} -DMODE=RESUME -DSXE=$<TARGET_FILE:SXE>
                 -DCONFIG_FULL=${TEST_DIR}/resume_full.ini -DDIR_FULL=${TEST_DIR}/resume_full
                 -DCONFIG_STOP=${TEST_DIR}/resume_stop.ini -DCONFIG_RESUME=${TEST_DIR}/resume_continue.ini
                 -DDIR_RESUME=${TEST_DIR}/resume_split -P ${SXE_TEST_SCRIPT})

# the same with open limit orders: the state keeps them
sxe_test_config(resume_limit_full LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_limit.ini RESUME 1)
sxe_test_config(resume_limit_stop LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_limit_stop.ini DIR resume_limit_split RESUME 1)
sxe_test_config(resume_limit_continue LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_limit.ini DIR resume_limit_split RESUME 1)
add_test(NAME resume_limit
         COMMAND ${CMAKE_COMMAND} -DMODE=RESUME -DSXE=$<TARGET_FILE:SXE>
                 -DCONFIG_FULL=${TEST_DIR}/resume_limit_full.ini -DDIR_FULL=${TEST_DIR}/resume_limit_full
                 -DCONFIG_STOP=${TEST_DIR}/resume_limit_stop.ini -DCONFIG_RESUME=${TEST_DIR}/resume_limit_continue.ini
                 -DDIR_RESUME=${TEST_DIR}/resume_limit_split -P ${SXE_TEST_SCRIPT})

#--- 3b. limit orders on hand-made candles ------------------------------------
# entry at 100 at the first step (no slippage), limit buy at 98, limit close at +2% of the average price.
# 01:00 the low touches 98: filled only with limit_fill = touch; 02:00 the low goes 0.3% below:
# filled with through (tick_step 0.2%); 03:00 the high reaches 104: the close is filled at its price.
# Limit fills at the limit price with the maker fee 0.02%; the cycle is closed by "limit".
foreach(LIMIT_FILL through touch)
    if(LIMIT_FILL STREQUAL "through")
        set(LIMIT_HOUR 02)
        set(LIMIT_ABSENT "^2024-01-01 01:[^|]*\\|FILL_BUY")
    else()
        set(LIMIT_HOUR 01)
        set(LIMIT_ABSENT "^2024-01-01 02:[^|]*\\|FILL_BUY")
    endif()
    sxe_test_config(limit_${LIMIT_FILL} LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                    STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_limit_small.ini SLIPPAGE 0
                    DATA_FILE BTCUSDT_limit_test.csv LIMIT_FILL ${LIMIT_FILL})
    add_test(NAME limit_${LIMIT_FILL}
             COMMAND ${CMAKE_COMMAND} -DMODE=ORDERS -DSXE=$<TARGET_FILE:SXE>
                     -DCONFIG=${TEST_DIR}/limit_${LIMIT_FILL}.ini -DDIR=${TEST_DIR}/limit_${LIMIT_FILL}
                     "-DMATCH1=^2024-01-01 01:00[^|]*\\|BUY\\|100.00\\|100.00\\|25.000000\\|1.5000\\|.*\\|market\\|\\|\\|0.000600$"
                     "-DMATCH2=^2024-01-01 ${LIMIT_HOUR}:[^|]*\\|FILL_BUY\\|[^|]*\\|98.00\\|12.747400\\|0.2498\\|.*\\|limit\\|1\\|98.00\\|0.000200$"
                     "-DMATCH3=\\|FILL_CLOSE\\|[^|]*\\|101.31\\|37.747400\\|0.7648\\|.*\\|limit\\|2\\|101.31\\|0.000200$"
                     "-DMATCH4=\\|MOVE\\|"
                     "-DABSENT=${LIMIT_ABSENT}" "-DRESULT_MATCH=^1\\|.*\\|limit$"
                     -P ${SXE_TEST_SCRIPT})
endforeach()

#--- 4. several engines in one process ---------------------------------------
# full run = light run (no files) = N light runs in parallel, same numbers
add_executable(SxeMultiRunTest MultiRun/SxeMultiRunTest.cpp)
target_link_libraries(SxeMultiRunTest PRIVATE Engine)

foreach(MULTI_TEST golden_template golden_isolated)
    add_test(NAME multirun_${MULTI_TEST}
             COMMAND SxeMultiRunTest ${TEST_DIR}/${MULTI_TEST}.ini ${TEST_DIR}/multirun_${MULTI_TEST} 4)
endforeach()

#--- 5. C interface (libSXE.so, sxe.h), from programs written in C ----------
enable_language(C)
foreach(API_PROGRAM SxeApiRun SxeApiTest)
    add_executable(${API_PROGRAM} Api/${API_PROGRAM}.c)
    target_link_libraries(${API_PROGRAM} PRIVATE SXEApi)
    set_target_properties(${API_PROGRAM} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endforeach()

# a run through the C interface gives the same cycles as the SXE program
sxe_test_config(api_golden_template LIBRARY libTemplate.so FOLDER ${TEMPLATE_DIR} STRATEGY_CONFIG ${TEMPLATE_CONFIG})
sxe_test_config(api_golden_isolated LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev10.ini)
sxe_test_config(api_golden_limit LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_limit.ini)
foreach(API_TEST template isolated limit)
    add_test(NAME api_golden_${API_TEST}
             COMMAND ${CMAKE_COMMAND} -DMODE=GOLDEN -DSXE=$<TARGET_FILE:SxeApiRun>
                     -DCONFIG=${TEST_DIR}/api_golden_${API_TEST}.ini -DDIR=${TEST_DIR}/api_golden_${API_TEST}
                     -DGOLDEN=${SXE_TEST_GOLDEN}/golden_${API_TEST}.txt -P ${SXE_TEST_SCRIPT})
endforeach()

# callbacks, options, configuration without files, errors
sxe_test_config(api_checks LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev10.ini)
add_test(NAME api_checks
         COMMAND SxeApiTest ${TEST_DIR}/api_checks.ini ${TEST_DIR}/api_checks ${TEST_DIR} libTestStrategy.so
                 ${TEST_DIR}/TestStrategy_lev10.ini ${SXE_TEST_SOURCE_DIR}/Data)

#--- 6. speed (measurement only) ---------------------------------------------
sxe_test_config(benchmark LIBRARY libTestStrategy.so FOLDER ${TEST_DIR}
                STRATEGY_CONFIG ${TEST_DIR}/TestStrategy_lev3.ini)
add_test(NAME benchmark
         COMMAND ${CMAKE_COMMAND} -DMODE=BENCH -DSXE=$<TARGET_FILE:SXE>
                 -DCONFIG=${TEST_DIR}/benchmark.ini -DDIR=${TEST_DIR}/benchmark -P ${SXE_TEST_SCRIPT})
set_tests_properties(benchmark PROPERTIES LABELS benchmark)
