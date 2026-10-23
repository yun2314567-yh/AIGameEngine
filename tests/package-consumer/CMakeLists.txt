cmake_minimum_required(VERSION 3.24)
project(TinyEnginePackageConsumer LANGUAGES CXX)

find_package(TinyEngine CONFIG REQUIRED)
add_executable(PackageConsumer main.cpp)
target_link_libraries(PackageConsumer PRIVATE TinyEngine::TinyEngine)

# Compile each installed public header as the first include in its own unit.
get_target_property(engineIncludes TinyEngine::TinyEngine INTERFACE_INCLUDE_DIRECTORIES)
foreach(directory IN LISTS engineIncludes)
    if(EXISTS "${directory}/TinyEngine")
        file(GLOB_RECURSE publicHeaders "${directory}/TinyEngine/*.h")
        foreach(header IN LISTS publicHeaders)
            file(RELATIVE_PATH relativeHeader "${directory}" "${header}")
            string(MAKE_C_IDENTIFIER "${relativeHeader}" unitName)
            set(unit "${CMAKE_CURRENT_BINARY_DIR}/${unitName}.cpp")
            file(WRITE "${unit}" "#include <${relativeHeader}>\n")
            target_sources(PackageConsumer PRIVATE "${unit}")
        endforeach()
    endif()
endforeach()

enable_testing()
add_test(NAME InstalledEngineRuns COMMAND PackageConsumer)
