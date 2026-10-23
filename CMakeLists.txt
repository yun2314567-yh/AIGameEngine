cmake_minimum_required(VERSION 3.24)

project(TinyEngine VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TINYENGINE_BUILD_EDITOR "Build the TinyEngine editor executable" ON)
option(TINYENGINE_BUILD_RUNTIME "Build the TinyEngine runtime executable" ON)
option(TINYENGINE_BUILD_TESTS "Build unit and integration tests" ON)
option(TINYENGINE_FETCH_DEPENDENCIES "Fetch and link optional production dependencies" OFF)
option(TINYENGINE_ENABLE_VULKAN "Enable Vulkan/NVRHI renderer integration" OFF)
option(TINYENGINE_ENABLE_GLFW "Enable GLFW window integration" OFF)
option(TINYENGINE_ENABLE_LUA "Enable Lua scripting integration" OFF)
option(TINYENGINE_ENABLE_MINIAUDIO "Enable miniaudio playback integration" OFF)
option(TINYENGINE_ENABLE_BULLET "Enable Bullet Physics integration" OFF)
option(TINYENGINE_ENABLE_CGLTF "Enable cgltf asset decoding integration" OFF)
option(TINYENGINE_ENABLE_GLM "Use glm math adapters when available" OFF)
option(TINYENGINE_ENABLE_EDITOR_UI "Enable ImGui/ImGuizmo editor integration" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(ShaderCompilation)
tinyengine_configure_dependencies()

add_subdirectory(engine)

if(TINYENGINE_ENABLE_VULKAN)
    tinyengine_add_shaders(TinyEngine
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Depth.vert.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Depth.frag.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Shadow.vert.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Shadow.frag.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Pbr.vert.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Pbr.frag.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Ssao.comp.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/SsaoBlur.comp.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/BufferTransform.comp.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/ToneMap.comp.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Present.vert.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/Present.frag.hlsl")
    get_target_property(TINYENGINE_SHADER_OUTPUTS TinyEngine TINYENGINE_SHADER_OUTPUTS)
    install(FILES ${TINYENGINE_SHADER_OUTPUTS}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/TinyEngine/shaders)
endif()

if(TINYENGINE_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(TINYENGINE_BUILD_RUNTIME)
    add_subdirectory(runtime)
endif()

if(TINYENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(DIRECTORY engine/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES THIRD_PARTY_NOTICES.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/TinyEngine)
install(DIRECTORY engine/shaders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/TinyEngine/shaders
    FILES_MATCHING PATTERN "*.hlsl" PATTERN "*.hlsli")
tinyengine_install_dependencies()
configure_package_config_file(cmake/TinyEngineConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/TinyEngineConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/TinyEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/TinyEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TinyEngineConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine)
install(EXPORT TinyEngineTargets
        FILE TinyEngineTargets.cmake
        NAMESPACE TinyEngine::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine)

message(STATUS "TinyEngine ${PROJECT_VERSION}")
message(STATUS "  Vulkan integration: ${TINYENGINE_ENABLE_VULKAN}")
message(STATUS "  GLFW integration:   ${TINYENGINE_ENABLE_GLFW}")
message(STATUS "  Lua integration:    ${TINYENGINE_ENABLE_LUA}")
message(STATUS "  Physics integration:${TINYENGINE_ENABLE_BULLET}")
message(STATUS "  cgltf integration:  ${TINYENGINE_ENABLE_CGLTF}")
message(STATUS "  miniaudio integration: ${TINYENGINE_ENABLE_MINIAUDIO}")
message(STATUS "  GLM integration:    ${TINYENGINE_ENABLE_GLM}")
message(STATUS "  Editor UI integration: ${TINYENGINE_ENABLE_EDITOR_UI}")
