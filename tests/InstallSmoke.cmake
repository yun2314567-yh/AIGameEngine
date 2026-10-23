if(NOT ENGINE_BUILD OR NOT SMOKE_ROOT)
    message(FATAL_ERROR "ENGINE_BUILD and SMOKE_ROOT are required")
endif()
if(NOT CONFIG)
    set(CONFIG Release)
endif()

function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Installed consumer failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
endfunction()

run_checked("${CMAKE_COMMAND}" --install "${ENGINE_BUILD}" --config "${CONFIG}"
    --prefix "${SMOKE_ROOT}/install")
if(NOT EXISTS "${SMOKE_ROOT}/install/share/TinyEngine/THIRD_PARTY_NOTICES.txt")
    message(FATAL_ERROR "Installed engine is missing third-party notices")
endif()
run_checked("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/package-consumer"
    -B "${SMOKE_ROOT}/consumer" "-DCMAKE_PREFIX_PATH=${SMOKE_ROOT}/install"
    "-DCMAKE_BUILD_TYPE=${CONFIG}")
# A single build worker also works in restricted Windows process environments.
run_checked("${CMAKE_COMMAND}" --build "${SMOKE_ROOT}/consumer" --config "${CONFIG}" --parallel 1)
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${SMOKE_ROOT}/consumer"
    -C "${CONFIG}" --output-on-failure)
message(STATUS "Installed package and standalone consumer passed")
