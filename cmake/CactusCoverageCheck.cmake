# Coverage gate driven by the coverage_check target.
#
# MODE=evaluate: SUMMARY_JSON, THRESHOLD, TESTS_FAILED — only the pass/fail decision.
# Otherwise: SOURCE_DIR, BINARY_DIR, PROFRAW_DIR, CTEST_COMMAND, LLVM_PROFDATA,
# LLVM_COV, OBJECTS_FILE (one instrumented binary per line), THRESHOLD.

function(cactus_coverage_evaluate summary_json threshold tests_failed)
    if(NOT threshold MATCHES "^[0-9]+(\\.[0-9]+)?$")
        message(FATAL_ERROR "Coverage threshold '${threshold}' is not a number")
    endif()
    file(READ "${summary_json}" summary)
    string(JSON percent GET "${summary}" data 0 totals lines percent)
    message("Gated line coverage: ${percent}% (threshold ${threshold}%)")

    set(failures)
    if(tests_failed)
        list(APPEND failures "tests failed")
    endif()
    if(percent LESS threshold)
        list(APPEND failures "gated line coverage is below the threshold; add tests for the uncovered lines")
    endif()
    if(failures)
        list(JOIN failures "; " reason)
        message(FATAL_ERROR "coverage_check failed: ${reason}")
    endif()
endfunction()

if(MODE STREQUAL "evaluate")
    cactus_coverage_evaluate("${SUMMARY_JSON}" "${THRESHOLD}" "${TESTS_FAILED}")
    return()
endif()

function(cactus_coverage_run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        list(GET ARGN 0 tool)
        message(FATAL_ERROR "${tool} failed (${result})")
    endif()
endfunction()

set(coverage_dir "${BINARY_DIR}/coverage")
set(profraw_dir "${PROFRAW_DIR}")
set(profdata "${coverage_dir}/cactus.profdata")
set(html_dir "${coverage_dir}/html")

# Profiles written before ctest (build-time fixture generation) must not count.
file(REMOVE_RECURSE "${profraw_dir}")
file(MAKE_DIRECTORY "${profraw_dir}")

cmake_host_system_information(RESULT jobs QUERY NUMBER_OF_LOGICAL_CORES)
execute_process(
    COMMAND "${CTEST_COMMAND}" --test-dir "${BINARY_DIR}" -j ${jobs} --output-on-failure
    RESULT_VARIABLE ctest_result
)
if(ctest_result EQUAL 0)
    set(tests_failed OFF)
else()
    set(tests_failed ON)
endif()

file(GLOB profraw_files "${profraw_dir}/*.profraw")
if(NOT profraw_files)
    message(FATAL_ERROR "coverage_check failed: no profiles were written to ${profraw_dir}")
endif()
# A file list, since hundreds of profile paths overflow the Windows command line.
list(JOIN profraw_files "\n" profraw_list)
file(WRITE "${coverage_dir}/profraw_files.txt" "${profraw_list}\n")
cactus_coverage_run("${LLVM_PROFDATA}" merge -sparse
    -f "${coverage_dir}/profraw_files.txt" -o "${profdata}")

file(STRINGS "${OBJECTS_FILE}" objects)
list(POP_FRONT objects primary_object)
list(TRANSFORM objects PREPEND "-object=")

set(gated_sources
    "${SOURCE_DIR}/src/common"
    "${SOURCE_DIR}/src/frontend"
    "${SOURCE_DIR}/src/backends/cpp-entt"
    "${SOURCE_DIR}/src/main.cpp"
)
set(runtime_sources
    "${SOURCE_DIR}/src/backends/cpp-entt/runtime.cpp"
    "${SOURCE_DIR}/src/backends/cpp-entt/runtime.hpp"
    "${SOURCE_DIR}/src/backends/cpp-entt/raylib_io.hpp"
    "${SOURCE_DIR}/src/backends/cpp-entt/spatial_query.hpp"
    "${SOURCE_DIR}/src/common/cactus_runtime.cpp"
    "${SOURCE_DIR}/src/common/cactus_runtime.hpp"
)
set(runtime_names)
foreach(runtime_source IN LISTS runtime_sources)
    get_filename_component(runtime_name "${runtime_source}" NAME)
    string(REPLACE "." "\\." runtime_name "${runtime_name}")
    list(APPEND runtime_names "${runtime_name}")
endforeach()
list(JOIN runtime_names "|" runtime_regex)
set(runtime_regex "[/\\\\](${runtime_regex})$")

set(profile_args "-instr-profile=${profdata}" "${primary_object}" ${objects})
set(gated_args "-ignore-filename-regex=${runtime_regex}" ${gated_sources})

function(cactus_coverage_export output_file)
    execute_process(
        COMMAND "${LLVM_COV}" export -summary-only ${profile_args} ${ARGN}
        OUTPUT_FILE "${output_file}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "llvm-cov export failed (${result})")
    endif()
endfunction()

set(gated_summary "${coverage_dir}/gated_summary.json")
set(runtime_summary "${coverage_dir}/runtime_summary.json")
cactus_coverage_export("${gated_summary}" ${gated_args})
cactus_coverage_export("${runtime_summary}" ${runtime_sources})

file(REMOVE_RECURSE "${html_dir}")
cactus_coverage_run("${LLVM_COV}" show -format=html "-output-dir=${html_dir}"
    ${profile_args} ${gated_args})

file(READ "${runtime_summary}" runtime_json)
string(JSON runtime_percent GET "${runtime_json}" data 0 totals lines percent)
message("Runtime line coverage (not gated): ${runtime_percent}%")
message("HTML report: ${html_dir}/index.html")
cactus_coverage_evaluate("${gated_summary}" "${THRESHOLD}" "${tests_failed}")
