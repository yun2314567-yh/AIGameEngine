include_guard(GLOBAL)

set(TINYENGINE_SHADER_COMPILER "AUTO" CACHE STRING "HLSL to SPIR-V compiler: AUTO, GLSLC, or DXC")
set_property(CACHE TINYENGINE_SHADER_COMPILER PROPERTY STRINGS AUTO GLSLC DXC)

function(_tinyengine_shader_stage source out_stage out_profile)
    get_filename_component(_shader_name "${source}" NAME)
    if(_shader_name MATCHES "\\.(vert|vs|vertex)\\.hlsl$")
        set(_stage vertex)
        set(_profile vs_6_0)
    elseif(_shader_name MATCHES "\\.(frag|ps|pixel)\\.hlsl$")
        set(_stage fragment)
        set(_profile ps_6_0)
    elseif(_shader_name MATCHES "\\.(comp|cs|compute)\\.hlsl$")
        set(_stage compute)
        set(_profile cs_6_0)
    else()
        message(FATAL_ERROR "Cannot infer shader stage from '${source}'. Name HLSL files *.vert.hlsl, *.frag.hlsl, or *.comp.hlsl.")
    endif()
    set(${out_stage} "${_stage}" PARENT_SCOPE)
    set(${out_profile} "${_profile}" PARENT_SCOPE)
endfunction()

function(tinyengine_add_shaders target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "tinyengine_add_shaders: target '${target}' does not exist")
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "COMPILER" "")
    if(ARG_UNPARSED_ARGUMENTS STREQUAL "")
        message(FATAL_ERROR "tinyengine_add_shaders(${target} ...) requires at least one HLSL source")
    endif()
    if(ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "tinyengine_add_shaders: missing value for ${ARG_KEYWORDS_MISSING_VALUES}")
    endif()

    string(TOUPPER "${ARG_COMPILER}" _requested_compiler)
    if(_requested_compiler STREQUAL "")
        set(_requested_compiler "${TINYENGINE_SHADER_COMPILER}")
    endif()
    string(TOUPPER "${_requested_compiler}" _requested_compiler)
    if(NOT _requested_compiler MATCHES "^(AUTO|GLSLC|DXC)$")
        message(FATAL_ERROR "TINYENGINE_SHADER_COMPILER must be AUTO, GLSLC, or DXC")
    endif()

    if(NOT Vulkan_GLSLC_EXECUTABLE)
        find_program(Vulkan_GLSLC_EXECUTABLE NAMES glslc glslc.exe
            HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    endif()
    if(NOT DXC_EXECUTABLE)
        find_program(DXC_EXECUTABLE NAMES dxc dxc.exe
            HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    endif()

    if(_requested_compiler STREQUAL "GLSLC")
        if(NOT Vulkan_GLSLC_EXECUTABLE)
            message(FATAL_ERROR "glslc was requested but not found; install the Vulkan SDK or set Vulkan_GLSLC_EXECUTABLE")
        endif()
        set(_shader_compiler "GLSLC")
    elseif(_requested_compiler STREQUAL "DXC")
        if(NOT DXC_EXECUTABLE)
            message(FATAL_ERROR "DXC was requested but not found; install the DirectX Shader Compiler or set DXC_EXECUTABLE")
        endif()
        set(_shader_compiler "DXC")
    elseif(Vulkan_GLSLC_EXECUTABLE)
        set(_shader_compiler "GLSLC")
    elseif(DXC_EXECUTABLE)
        set(_shader_compiler "DXC")
    else()
        message(FATAL_ERROR "Neither glslc nor DXC was found; install the Vulkan SDK or set TINYENGINE_SHADER_COMPILER")
    endif()

    set(_tinyengine_shader_outputs)
    set(_tinyengine_shader_includes
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../engine/shaders/Common.hlsli"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../engine/shaders/Geometry.hlsli"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../engine/shaders/Material.hlsli")
    foreach(_shader IN LISTS ARG_UNPARSED_ARGUMENTS)
        if(NOT EXISTS "${_shader}")
            message(FATAL_ERROR "Shader source does not exist: ${_shader}")
        endif()
        _tinyengine_shader_stage("${_shader}" _shader_stage _shader_profile)
        get_filename_component(_shader_name "${_shader}" NAME)
        get_filename_component(_shader_dir "${_shader}" DIRECTORY)
        set(_shader_output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_shader_name}.spv")
        get_filename_component(_shader_output_dir "${_shader_output}" DIRECTORY)

        if(_shader_compiler STREQUAL "GLSLC")
            add_custom_command(
                OUTPUT "${_shader_output}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_output_dir}"
                COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.2 -O
                    -x hlsl -fshader-stage=${_shader_stage}
                    -I "${_shader_dir}" -I "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../engine/shaders"
                    -o "${_shader_output}" "${_shader}"
                DEPENDS "${_shader}" ${_tinyengine_shader_includes}
                COMMENT "Compiling HLSL shader ${_shader_name} with glslc"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT "${_shader_output}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_output_dir}"
                COMMAND "${DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
                    -fvk-use-dx-layout -Zpc -E main -T ${_shader_profile}
                    -I "${_shader_dir}" -I "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../engine/shaders"
                    -Fo "${_shader_output}" "${_shader}"
                DEPENDS "${_shader}" ${_tinyengine_shader_includes}
                COMMENT "Compiling HLSL shader ${_shader_name} with DXC"
                VERBATIM)
        endif()
        list(APPEND _tinyengine_shader_outputs "${_shader_output}")
    endforeach()

    set(_shader_target "${target}_shaders")
    add_custom_target("${_shader_target}" DEPENDS ${_tinyengine_shader_outputs})
    add_dependencies("${target}" "${_shader_target}")
    set_property(TARGET "${target}" APPEND PROPERTY TINYENGINE_SHADER_OUTPUTS "${_tinyengine_shader_outputs}")
endfunction()

function(tinyengine_add_shader_compilation target)
    tinyengine_add_shaders(${target} ${ARGN})
endfunction()
