cmake_minimum_required(VERSION 3.11) # FetchContent is available in 3.11+
project(cpick)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
set(RAYLIB_VERSION 5.5)

FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
    FIND_PACKAGE_ARGS
)

FetchContent_MakeAvailable(raylib)

# Our Project
add_executable(${PROJECT_NAME} cpick.c shapes.c)
#set(raylib_VERBOSE 1)

target_link_libraries(${PROJECT_NAME} raylib)
if(UNIX AND NOT APPLE)  # or: if(NOT WIN32)
  target_link_libraries(${PROJECT_NAME} m)
endif()

if(MSVC)
    # Supress warnings for integer-float conversions. Otherwise, the default
    # /W3 warnings are enabled.
    target_compile_options(${PROJECT_NAME} PRIVATE /wd4244)
endif()

# Web Configurations
if (EMSCRIPTEN)
    # Tell Emscripten to build an example.html file.
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
endif()

# Checks if OSX and links appropriate frameworks (Only required on MacOS)
if (APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework IOKit")
    target_link_libraries(${PROJECT_NAME} "-framework Cocoa")
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL")
endif()
