add_executable(TinyEngineTests
    CoreTests.cpp
    EngineIntegrationTests.cpp
    RendererTests.cpp
)
target_link_libraries(TinyEngineTests PRIVATE TinyEngine)
target_compile_features(TinyEngineTests PRIVATE cxx_std_20)
target_compile_definitions(TinyEngineTests PRIVATE TINYENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME TinyEngineTests COMMAND TinyEngineTests)

add_executable(TinyEngineAutomationTests AutomationTests.cpp)
target_link_libraries(TinyEngineAutomationTests PRIVATE TinyEngine)
target_compile_features(TinyEngineAutomationTests PRIVATE cxx_std_20)
target_compile_definitions(TinyEngineAutomationTests PRIVATE TINYENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME TinyEngineAutomationTests COMMAND TinyEngineAutomationTests)

add_executable(TinyEngineAudioTests AudioTests.cpp)
target_link_libraries(TinyEngineAudioTests PRIVATE TinyEngine)
target_compile_definitions(TinyEngineAudioTests PRIVATE TINYENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME TinyEngineAudioTests COMMAND TinyEngineAudioTests)

if(MSVC)
    target_compile_options(TinyEngineTests PRIVATE /UNDEBUG)
    target_compile_options(TinyEngineAutomationTests PRIVATE /UNDEBUG)
    target_compile_options(TinyEngineAudioTests PRIVATE /UNDEBUG)
else()
    target_compile_options(TinyEngineTests PRIVATE -UNDEBUG)
    target_compile_options(TinyEngineAutomationTests PRIVATE -UNDEBUG)
    target_compile_options(TinyEngineAudioTests PRIVATE -UNDEBUG)
endif()
set_tests_properties(TinyEngineTests TinyEngineAutomationTests TinyEngineAudioTests PROPERTIES TIMEOUT 30)

add_test(NAME TinyEngineInstalledConsumer
    COMMAND "${CMAKE_COMMAND}"
        "-DENGINE_BUILD=${PROJECT_BINARY_DIR}"
        "-DSMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install-smoke-$<CONFIG>"
        "-DCONFIG=$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/InstallSmoke.cmake")
set_tests_properties(TinyEngineInstalledConsumer PROPERTIES TIMEOUT 180)

if(TINYENGINE_ENABLE_VULKAN AND TINYENGINE_ENABLE_GLFW)
    add_executable(TinyEngineGpuTests GpuTests.cpp)
    target_link_libraries(TinyEngineGpuTests PRIVATE TinyEngine)
    target_compile_definitions(TinyEngineGpuTests PRIVATE
        TINYENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        TINYENGINE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders"
        TINYENGINE_CAPTURE_DIRECTORY="${PROJECT_BINARY_DIR}")
    option(TINYENGINE_RUN_GPU_TESTS "Run tests requiring a Vulkan device and a display" OFF)
    if(TINYENGINE_RUN_GPU_TESTS)
        add_test(NAME TinyEngineGpuTests COMMAND TinyEngineGpuTests)
        set_tests_properties(TinyEngineGpuTests PROPERTIES TIMEOUT 60 LABELS GPU)
        if(TINYENGINE_ENABLE_EDITOR_UI AND TARGET TinyEngineEditor)
            add_test(NAME TinyEngineGraphicalEditor
                COMMAND TinyEngineEditor --scene "${CMAKE_SOURCE_DIR}/assets/scenes/FeatureShowcase.tiny"
                    --asset-root "${CMAKE_SOURCE_DIR}/assets" --frames 3)
            set_tests_properties(TinyEngineGraphicalEditor PROPERTIES
                TIMEOUT 60 LABELS GPU PASS_REGULAR_EXPRESSION "Frames: 3")
        endif()
    endif()
endif()

if(TARGET TinyEngineEditor)
    add_test(NAME TinyEngineEditorRejectsInvalidFrameLimit COMMAND TinyEngineEditor --frames invalid)
    set_tests_properties(TinyEngineEditorRejectsInvalidFrameLimit PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
endif()

if(TARGET TinyEngineRuntime)
    add_executable(TinyEngineExportTests ExportTests.cpp)
    target_link_libraries(TinyEngineExportTests PRIVATE TinyEngine)
    target_compile_definitions(TinyEngineExportTests PRIVATE TINYENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME TinyEngineExportedRuntime
        COMMAND "${CMAKE_COMMAND}"
            "-DEXPORTER=$<TARGET_FILE:TinyEngineExportTests>"
            "-DRUNTIME=$<TARGET_FILE:TinyEngineRuntime>"
            "-DPACKAGE=${CMAKE_CURRENT_BINARY_DIR}/exported-game-$<CONFIG>"
            "-DWORK_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ExportSmoke.cmake")
    set_tests_properties(TinyEngineExportedRuntime PROPERTIES TIMEOUT 45)
    add_test(NAME TinyEngineRuntimeHeadless COMMAND TinyEngineRuntime --headless --frames 3)
    add_test(NAME TinyEngineRuntimeSceneLoad
        COMMAND TinyEngineRuntime --scene "${CMAKE_CURRENT_SOURCE_DIR}/assets/RuntimeSmoke.tiny" --headless --frames 2)
    add_test(NAME TinyEngineRuntimeRejectsInvalidFrameLimit COMMAND TinyEngineRuntime --frames invalid)
    set_tests_properties(TinyEngineRuntimeHeadless PROPERTIES PASS_REGULAR_EXPRESSION "Frames: 3")
    set_tests_properties(TinyEngineRuntimeSceneLoad PROPERTIES PASS_REGULAR_EXPRESSION "Frames: 2")
    set_tests_properties(TinyEngineRuntimeRejectsInvalidFrameLimit PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(TinyEngineRuntimeHeadless TinyEngineRuntimeSceneLoad TinyEngineRuntimeRejectsInvalidFrameLimit
        PROPERTIES TIMEOUT 30)
    if(TINYENGINE_ENABLE_LUA AND TARGET lua::lua)
        add_test(NAME TinyEngineRuntimeShowcase
            COMMAND TinyEngineRuntime --scene "${CMAKE_SOURCE_DIR}/assets/scenes/FeatureShowcase.tiny"
                --asset-root "${CMAKE_SOURCE_DIR}/assets" --headless --frames 2)
        set_tests_properties(TinyEngineRuntimeShowcase PROPERTIES PASS_REGULAR_EXPRESSION "Frames: 2" TIMEOUT 30)
    endif()
endif()
