foreach(required EXPORTER RUNTIME PACKAGE WORK_DIRECTORY)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing export test argument: ${required}")
    endif()
endforeach()
execute_process(COMMAND "${EXPORTER}" "${RUNTIME}" "${PACKAGE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Export failed (${result}): ${output}\n${error}")
endif()
get_filename_component(runtime_name "${RUNTIME}" NAME)
execute_process(COMMAND "${PACKAGE}/${runtime_name}" --headless --frames 3
    WORKING_DIRECTORY "${WORK_DIRECTORY}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT result EQUAL 0 OR NOT output MATCHES "Frames: 3")
    message(FATAL_ERROR "Exported runtime failed (${result}): ${output}\n${error}")
endif()
message(STATUS "Packaged runtime launched from an unrelated working directory: ${output}")
