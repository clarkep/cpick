cmake_minimum_required(VERSION 3.11) # FetchContent is available in 3.11+
include(FetchContent)
# Fix warning about setting timestamps when downloading dependencies
cmake_policy(SET CMP0135 NEW)

project(quickpick)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SYSTEM_LIBS "Use system libraries instead of fetching at configure time." OFF)

if (${SYSTEM_LIBS})
    find_package(SDL2 REQUIRED CONFIG REQUIRED COMPONENTS SDL2)
    find_package(Freetype REQUIRED)
    add_library(freetype ALIAS Freetype::Freetype)
else()
#     FetchContent_Declare(
#        SDL2
#        GIT_REPOSITORY https://github.com/libsdl-org/SDL
#        GIT_TAG release-2.30.11
#    )
    FetchContent_Declare(
        SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
        URL_HASH MD5=bea190b480f6df249db29eb3bacfe41e
    )
    FetchContent_MakeAvailable(SDL2)
    FetchContent_Declare(
        Freetype
        URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.gz
        URL_HASH MD5=ac1f0b517f62bd40d50bc995faa5741d
    )
    FetchContent_MakeAvailable(Freetype)
    get_property(allTargets GLOBAL PROPERTY TARGETS)
    message(STATUS "Targets: ${allTargets}")
endif()

add_executable(${PROJECT_NAME} quickpick.c draw.c util.c glad.c)

target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
target_link_libraries(${PROJECT_NAME} PRIVATE freetype)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(UNIX AND NOT APPLE)  # or: if(NOT WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE m)
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
