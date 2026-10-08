cmake_minimum_required(VERSION 3.28)

project(tdms_viewer
    VERSION 0.1.0
    DESCRIPTION "TDMS viewer"
    LANGUAGES CXX
)

if(NOT MINGW)
    message(FATAL_ERROR
        "tdms_viewer is a MinGW project. Configure with the mingw or mingw-wsl preset.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# GLFW, ImGui, and ImPlot versions match the FetchContent example shipped with ImPlot v1.0.
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.8
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG v1.92.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot
    GIT_TAG v1.0
    GIT_SHALLOW TRUE
)

find_package(Git REQUIRED)

# master has no tags. This commit is the tip used by the viewer.
FetchContent_Declare(
    tdmspp
    GIT_REPOSITORY https://github.com/rubdos/TDMSpp
    GIT_TAG 67a22fd35129ae7d21698c1083cd73db7bd10527
    GIT_CONFIG core.autocrlf=false
    SOURCE_SUBDIR src
    PATCH_COMMAND ${GIT_EXECUTABLE} apply ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tdmspp.patch
)

FetchContent_MakeAvailable(glfw imgui implot tdmspp)

find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

target_include_directories(tdmspp PUBLIC ${tdmspp_SOURCE_DIR}/src)

add_executable(tdms_viewer
    src/main.cpp
    src/export_range.cpp
)

target_link_libraries(tdms_viewer PRIVATE implot tdmspp comdlg32)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(tdms_viewer PRIVATE -static)
endif()

target_compile_options(tdms_viewer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(tdms_continued_segment_count
    tests/continued_segment_count.cpp
)
target_link_libraries(tdms_continued_segment_count PRIVATE tdmspp)
target_compile_options(tdms_continued_segment_count PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(tdms_export_range
    tests/export_range.cpp
    src/export_range.cpp
)
target_include_directories(tdms_export_range PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tdms_export_range PRIVATE tdmspp)
target_compile_options(tdms_export_range PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
