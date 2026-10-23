add_library(TinyEngine STATIC
    src/Core.cpp
    src/Input.cpp
    src/ECS.cpp
    src/Math.cpp
    src/Physics.cpp
    src/Prefab.cpp
    src/Scene.cpp
    src/Scripting.cpp
    src/Assets/GltfImporter.cpp
    src/Assets/PbrMaterial.cpp
    src/Audio/Audio.cpp
    src/Editor/EngineHost.cpp
    src/Editor/Automation.cpp
    src/Editor/CommandProtocol.cpp
    src/Platform/Window.cpp
    src/Platform/Paths.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/Environment.cpp
    src/Renderer/GraphicsContext.cpp
    src/Renderer/VulkanGraphicsContext.cpp
)
add_library(TinyEngine::TinyEngine ALIAS TinyEngine)
set_target_properties(TinyEngine PROPERTIES EXPORT_NAME TinyEngine)

file(READ "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" TINYENGINE_THIRD_PARTY_NOTICES)
# Keep individual literals below MSVC's limit while preserving every newline.
string(REPLACE "\n" "\n)tinyengine\"\nR\"tinyengine(" TINYENGINE_THIRD_PARTY_NOTICES "${TINYENGINE_THIRD_PARTY_NOTICES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt")
configure_file("${PROJECT_SOURCE_DIR}/cmake/ThirdPartyNotices.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ThirdPartyNotices.h" @ONLY)
target_include_directories(TinyEngine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_features(TinyEngine PUBLIC cxx_std_20)
target_include_directories(TinyEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if (MSVC)
    target_compile_options(TinyEngine PRIVATE /W4 /permissive-)
else()
    target_compile_options(TinyEngine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(TINYENGINE_ENABLE_GLFW AND TARGET glfw)
    target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_GLFW=1)
    target_link_libraries(TinyEngine PRIVATE glfw)
endif()

if(TINYENGINE_ENABLE_MINIAUDIO AND TARGET miniaudio::miniaudio)
    target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_MINIAUDIO=1)
    target_link_libraries(TinyEngine PRIVATE miniaudio::miniaudio)
endif()

if(TINYENGINE_ENABLE_CGLTF AND TARGET cgltf::cgltf)
    target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_CGLTF=1)
    target_link_libraries(TinyEngine PRIVATE "$<BUILD_INTERFACE:cgltf::cgltf>")
endif()

if(TINYENGINE_ENABLE_LUA AND TARGET lua::lua)
    target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_LUA=1)
    target_link_libraries(TinyEngine PRIVATE lua::lua)
endif()

if(TINYENGINE_ENABLE_BULLET AND TARGET Bullet::Bullet)
    target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_BULLET=1)
    target_link_libraries(TinyEngine PRIVATE Bullet::Bullet)
endif()

if(TINYENGINE_ENABLE_VULKAN AND TARGET Vulkan::Vulkan AND TARGET NVRHI::Core AND TARGET NVRHI::Vulkan)
    # Vulkan-Hpp's dynamic dispatcher is shared with NVRHI and must use the
    # same SDK headers. Keep the selected SDK ahead of implicit package paths.
    set_property(TARGET TinyEngine PROPERTY NO_SYSTEM_FROM_IMPORTED TRUE)
    target_include_directories(TinyEngine BEFORE PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_VULKAN_NVRHI=1
        TINYENGINE_BUILD_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders")
    target_link_libraries(TinyEngine PRIVATE Vulkan::Vulkan NVRHI::Core NVRHI::Vulkan)
    if(TARGET tinyengine_stb)
        target_link_libraries(TinyEngine PRIVATE "$<BUILD_INTERFACE:tinyengine_stb>")
    endif()
    if(TINYENGINE_ENABLE_EDITOR_UI AND TARGET imgui::backends)
        target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_IMGUI_VULKAN=1)
        target_link_libraries(TinyEngine PRIVATE imgui::backends)
    endif()
endif()

if(TINYENGINE_ENABLE_GLM AND TARGET glm::glm)
    target_compile_definitions(TinyEngine PRIVATE TINYENGINE_WITH_GLM=1 GLM_ENABLE_EXPERIMENTAL=1)
    target_link_libraries(TinyEngine PRIVATE "$<BUILD_INTERFACE:glm::glm>")
endif()

install(TARGETS TinyEngine EXPORT TinyEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
