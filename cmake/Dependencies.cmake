include_guard(GLOBAL)

include(FetchContent)

function(tinyengine_require_dependency option_name)
    if(NOT ${option_name})
        return()
    endif()
    foreach(dependency IN LISTS ARGN)
        if(NOT TARGET "${dependency}")
            message(FATAL_ERROR "${option_name}=ON requires target ${dependency}. Set TINYENGINE_FETCH_DEPENDENCIES=ON to build the pinned integration dependencies.")
        endif()
    endforeach()
endfunction()

function(tinyengine_register_dependency)
    foreach(dependency IN LISTS ARGN)
        if(NOT TARGET "${dependency}")
            message(FATAL_ERROR "Missing TinyEngine dependency target: ${dependency}")
        endif()
        get_target_property(imported "${dependency}" IMPORTED)
        if(NOT imported)
            set_property(GLOBAL APPEND PROPERTY TINYENGINE_INSTALL_DEPENDENCIES "${dependency}")
        endif()
    endforeach()
endfunction()

function(tinyengine_configure_dependencies)
    if(TINYENGINE_ENABLE_VULKAN OR TINYENGINE_ENABLE_EDITOR_UI)
        find_package(Vulkan REQUIRED)
    endif()

    if(TINYENGINE_FETCH_DEPENDENCIES)
        # Select integrations explicitly. Archives avoid fetching every upstream
        # branch and tag when only a single pinned source version is needed.
        if(TINYENGINE_ENABLE_GLFW OR TINYENGINE_ENABLE_EDITOR_UI)
            set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(glfw
                URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
            FetchContent_MakeAvailable(glfw)
            tinyengine_register_dependency(glfw)
        endif()

        if(TINYENGINE_ENABLE_GLM)
            set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
            set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(glm
                URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
            FetchContent_MakeAvailable(glm)
        endif()

        if(TINYENGINE_ENABLE_MINIAUDIO)
            FetchContent_Declare(miniaudio
                URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.21.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR tinyengine-no-project)
            FetchContent_MakeAvailable(miniaudio)
            add_library(tinyengine_miniaudio INTERFACE)
            target_include_directories(tinyengine_miniaudio INTERFACE "$<BUILD_INTERFACE:${miniaudio_SOURCE_DIR}>")
            find_package(Threads REQUIRED)
            target_link_libraries(tinyengine_miniaudio INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
            if(UNIX)
                target_link_libraries(tinyengine_miniaudio INTERFACE m)
            endif()
            add_library(miniaudio::miniaudio ALIAS tinyengine_miniaudio)
            tinyengine_register_dependency(tinyengine_miniaudio)
        endif()

        if(TINYENGINE_ENABLE_LUA)
            enable_language(C)
            FetchContent_Declare(lua
                URL https://github.com/lua/lua/archive/refs/tags/v5.4.7.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR tinyengine-no-project)
            FetchContent_MakeAvailable(lua)
            set(lua_sources
                lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c
                lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c
                ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c
                liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c
                linit.c)
            list(TRANSFORM lua_sources PREPEND "${lua_SOURCE_DIR}/")
            add_library(tinyengine_lua STATIC ${lua_sources})
            target_include_directories(tinyengine_lua PUBLIC "$<BUILD_INTERFACE:${lua_SOURCE_DIR}>")
            if(UNIX)
                target_compile_definitions(tinyengine_lua PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
                target_link_libraries(tinyengine_lua PUBLIC ${CMAKE_DL_LIBS} m)
            endif()
            add_library(lua::lua ALIAS tinyengine_lua)
            tinyengine_register_dependency(tinyengine_lua)
        endif()

        if(TINYENGINE_ENABLE_BULLET)
            # Bullet 3.25 still declares CMake 2.4 compatibility. CMake 4 needs
            # an explicit policy floor for that pinned upstream project.
            set(saved_policy_minimum "${CMAKE_POLICY_VERSION_MINIMUM}")
            if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND
               (NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS 3.5))
                set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
            endif()
            set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
            set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
            set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
            set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
            set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
            set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
            set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
            set(BUILD_PYBULLET OFF CACHE BOOL "" FORCE)
            set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
            set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
            set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(bullet
                URL https://github.com/bulletphysics/bullet3/archive/refs/tags/3.25.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
            FetchContent_MakeAvailable(bullet)
            set(CMAKE_POLICY_VERSION_MINIMUM "${saved_policy_minimum}")
            if(MSVC)
                foreach(bullet_target BulletDynamics BulletCollision LinearMath)
                    set_property(TARGET ${bullet_target} PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
                endforeach()
            endif()
            add_library(tinyengine_bullet INTERFACE)
            target_link_libraries(tinyengine_bullet INTERFACE BulletDynamics BulletCollision LinearMath)
            target_include_directories(tinyengine_bullet INTERFACE "$<BUILD_INTERFACE:${bullet_SOURCE_DIR}/src>")
            add_library(Bullet::Bullet ALIAS tinyengine_bullet)
            tinyengine_register_dependency(tinyengine_bullet BulletDynamics BulletCollision LinearMath)
        endif()

        if(TINYENGINE_ENABLE_CGLTF)
            FetchContent_Declare(cgltf
                URL https://github.com/jkuhlmann/cgltf/archive/refs/tags/v1.14.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR tinyengine-no-project)
            FetchContent_MakeAvailable(cgltf)
            add_library(cgltf_headers INTERFACE)
            target_include_directories(cgltf_headers INTERFACE "$<BUILD_INTERFACE:${cgltf_SOURCE_DIR}>")
            add_library(cgltf::cgltf ALIAS cgltf_headers)
            tinyengine_register_dependency(cgltf_headers)
        endif()

        if(TINYENGINE_ENABLE_VULKAN)
            FetchContent_Declare(stb
                URL https://github.com/nothings/stb/archive/f0569113c93ad095470c54bf34a17b36646bbbb5.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR tinyengine-no-project)
            FetchContent_MakeAvailable(stb)
            add_library(tinyengine_stb INTERFACE)
            target_include_directories(tinyengine_stb INTERFACE "$<BUILD_INTERFACE:${stb_SOURCE_DIR}>")
            tinyengine_register_dependency(tinyengine_stb)
            set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
            set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
            set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
            set(NVRHI_WITH_NVAPI OFF CACHE BOOL "" FORCE)
            set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
            set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(nvrhi
                URL https://github.com/NVIDIA-RTX/NVRHI/archive/6b96fb03e07539f08327aea76c56d55f1de9d906.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
            FetchContent_MakeAvailable(nvrhi)
            if(TARGET nvrhi_vk)
                set_property(TARGET nvrhi_vk PROPERTY NO_SYSTEM_FROM_IMPORTED TRUE)
                target_include_directories(nvrhi_vk BEFORE PRIVATE ${Vulkan_INCLUDE_DIRS})
                add_library(NVRHI::Vulkan ALIAS nvrhi_vk)
                tinyengine_register_dependency(nvrhi_vk)
            elseif(TARGET nvrhi_vulkan)
                add_library(NVRHI::Vulkan ALIAS nvrhi_vulkan)
                tinyengine_register_dependency(nvrhi_vulkan)
            endif()
            if(TARGET nvrhi)
                # Upstream forces /MT for shared builds; engine and adapters use
                # the configured runtime, so all NVRHI targets must match it.
                if(MSVC)
                    if(CMAKE_MSVC_RUNTIME_LIBRARY)
                        set_property(TARGET nvrhi PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
                    else()
                        set_property(TARGET nvrhi PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
                    endif()
                endif()
                add_library(NVRHI::Core ALIAS nvrhi)
                tinyengine_register_dependency(nvrhi)
            endif()
        endif()

        if(TINYENGINE_ENABLE_EDITOR_UI)
            FetchContent_Declare(imgui
                URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR tinyengine-no-project)
            FetchContent_Declare(imguizmo
                URL https://github.com/CedricGuillemet/ImGuizmo/archive/refs/tags/1.10.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR tinyengine-no-project)
            FetchContent_MakeAvailable(imgui imguizmo)
            add_library(tinyengine_imgui STATIC
                "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
                "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
            target_include_directories(tinyengine_imgui PUBLIC "$<BUILD_INTERFACE:${imgui_SOURCE_DIR}>")
            add_library(imgui::imgui ALIAS tinyengine_imgui)
            add_library(tinyengine_imgui_backends STATIC
                "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
                "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
            target_compile_definitions(tinyengine_imgui_backends PRIVATE GLFW_INCLUDE_NONE)
            target_link_libraries(tinyengine_imgui_backends PUBLIC imgui::imgui glfw Vulkan::Vulkan)
            target_include_directories(tinyengine_imgui_backends PUBLIC "$<BUILD_INTERFACE:${imgui_SOURCE_DIR}>")
            add_library(imgui::backends ALIAS tinyengine_imgui_backends)
            add_library(tinyengine_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
            target_link_libraries(tinyengine_imguizmo PUBLIC imgui::imgui)
            target_include_directories(tinyengine_imguizmo PUBLIC "$<BUILD_INTERFACE:${imguizmo_SOURCE_DIR}/src>")
            target_compile_definitions(tinyengine_imguizmo PRIVATE IMGUI_DEFINE_MATH_OPERATORS=1)
            add_library(ImGuizmo::ImGuizmo ALIAS tinyengine_imguizmo)
            tinyengine_register_dependency(tinyengine_imgui tinyengine_imgui_backends tinyengine_imguizmo)
        endif()
    endif()

    tinyengine_require_dependency(TINYENGINE_ENABLE_GLFW glfw)
    tinyengine_require_dependency(TINYENGINE_ENABLE_GLM glm::glm)
    tinyengine_require_dependency(TINYENGINE_ENABLE_MINIAUDIO miniaudio::miniaudio)
    tinyengine_require_dependency(TINYENGINE_ENABLE_LUA lua::lua)
    tinyengine_require_dependency(TINYENGINE_ENABLE_BULLET Bullet::Bullet)
    tinyengine_require_dependency(TINYENGINE_ENABLE_CGLTF cgltf::cgltf)
    tinyengine_require_dependency(TINYENGINE_ENABLE_VULKAN Vulkan::Vulkan NVRHI::Core NVRHI::Vulkan)
    tinyengine_require_dependency(TINYENGINE_ENABLE_EDITOR_UI imgui::imgui imgui::backends ImGuizmo::ImGuizmo)
endfunction()

function(tinyengine_install_dependencies)
    get_property(dependencies GLOBAL PROPERTY TINYENGINE_INSTALL_DEPENDENCIES)
    if(NOT dependencies)
        return()
    endif()
    list(REMOVE_DUPLICATES dependencies)
    foreach(dependency IN LISTS dependencies)
        # Private backend includes are needed during this build, while the
        # public installed engine headers do not expose those upstream types.
        get_target_property(includes "${dependency}" INTERFACE_INCLUDE_DIRECTORIES)
        if(includes)
            set(install_safe_includes)
            foreach(directory IN LISTS includes)
                if(directory MATCHES "^\\$<INSTALL_INTERFACE:")
                    continue()
                elseif(directory MATCHES "^\\$<")
                    list(APPEND install_safe_includes "${directory}")
                else()
                    list(APPEND install_safe_includes "$<BUILD_INTERFACE:${directory}>")
                endif()
            endforeach()
            set_property(TARGET "${dependency}" PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${install_safe_includes}")
        endif()
    endforeach()
    install(TARGETS ${dependencies} EXPORT TinyEngineTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()
