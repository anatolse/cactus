# Runs the threshold evaluation of cmake/CactusCoverageCheck.cmake on one fixture
# summary and asserts both its exit status and its printed output.
#
# Inputs: CHECK_SCRIPT, SUMMARY_JSON, THRESHOLD, TESTS_FAILED, EXPECT (pass|fail),
# EXPECT_OUTPUT (regex).

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -DMODE=evaluate
            "-DSUMMARY_JSON=${SUMMARY_JSON}"
            "-DTHRESHOLD=${THRESHOLD}"
            "-DTESTS_FAILED=${TESTS_FAILED}"
            -P "${CHECK_SCRIPT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
message("${output}")

if(EXPECT STREQUAL "pass" AND NOT result EQUAL 0)
    message(FATAL_ERROR "expected exit 0, got ${result}")
endif()
if(EXPECT STREQUAL "fail" AND result EQUAL 0)
    message(FATAL_ERROR "expected non-zero exit, got 0")
endif()
if(NOT output MATCHES "${EXPECT_OUTPUT}")
    message(FATAL_ERROR "output does not match '${EXPECT_OUTPUT}'")
endif()
