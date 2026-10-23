add_executable(TinyEngineEditor src/EditorMain.cpp)
target_link_libraries(TinyEngineEditor PRIVATE TinyEngine)
target_compile_features(TinyEngineEditor PRIVATE cxx_std_20)
target_compile_definitions(TinyEngineEditor PRIVATE TINYENGINE_EDITOR_BUILD=1)
if(TINYENGINE_ENABLE_EDITOR_UI)
    target_compile_definitions(TinyEngineEditor PRIVATE TINYENGINE_EDITOR_UI_REQUESTED=1)
endif()
if(TINYENGINE_ENABLE_EDITOR_UI AND TINYENGINE_ENABLE_VULKAN AND
   TARGET imgui::imgui AND TARGET imgui::backends AND TARGET ImGuizmo::ImGuizmo AND TARGET glfw)
    target_sources(TinyEngineEditor PRIVATE src/EditorUi.cpp)
    target_link_libraries(TinyEngineEditor PRIVATE imgui::imgui imgui::backends ImGuizmo::ImGuizmo glfw)
    target_compile_definitions(TinyEngineEditor PRIVATE
        TINYENGINE_WITH_EDITOR_UI=1
        TINYENGINE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders")
endif()
install(TARGETS TinyEngineEditor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
