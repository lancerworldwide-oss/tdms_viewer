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

set(IIR1_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(IIR1_BUILD_DEMO OFF CACHE BOOL "" FORCE)

# Tag 1.10.0. Link the static library only. The shared library stays out of the build.
FetchContent_Declare(
    iir1
    GIT_REPOSITORY https://github.com/berndporr/iir1
    GIT_TAG 1.10.0
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(glfw imgui implot tdmspp iir1)

# Header-only. There is no CMakeLists.txt, so MakeAvailable cannot add it.
FetchContent_Declare(
    pocketfft
    GIT_REPOSITORY https://github.com/mreineck/pocketfft
    GIT_TAG c90e55b3d529f8efa40ed01a20de22405f45fc65
)
FetchContent_GetProperties(pocketfft)
if(NOT pocketfft_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(pocketfft)
endif()

find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
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
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

target_include_directories(tdmspp PUBLIC ${tdmspp_SOURCE_DIR}/src)

add_executable(tdms_viewer
    src/main.cpp
    src/export_range.cpp
    src/rolling_rms.cpp
    src/duration.cpp
    src/frequency.cpp
    src/fft.cpp
    src/iir_filter.cpp
)

target_link_libraries(tdms_viewer PRIVATE implot tdmspp iir::iir_static comdlg32)

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

add_executable(tdms_rolling_rms
    tests/rolling_rms.cpp
    src/rolling_rms.cpp
)
target_include_directories(tdms_rolling_rms PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tdms_rolling_rms PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(tdms_rolling_abs_mean
    tests/rolling_abs_mean.cpp
)
target_include_directories(tdms_rolling_abs_mean PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tdms_rolling_abs_mean PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(tdms_duration
    tests/duration.cpp
    src/duration.cpp
)
target_include_directories(tdms_duration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tdms_duration PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(tdms_frequency
    tests/frequency.cpp
    src/frequency.cpp
)
target_include_directories(tdms_frequency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tdms_frequency PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(tdms_fft
    tests/fft.cpp
    src/fft.cpp
)
target_include_directories(tdms_fft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tdms_fft PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(tdms_iir_filter
    tests/iir_filter.cpp
    src/iir_filter.cpp
)
target_include_directories(tdms_iir_filter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tdms_iir_filter PRIVATE iir::iir_static)
target_compile_options(tdms_iir_filter PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

set_source_files_properties(src/fft.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${pocketfft_SOURCE_DIR}"
)
set_source_files_properties(tests/fft.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
