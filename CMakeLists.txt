cmake_minimum_required(VERSION 3.20)
project(oak-explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Dependencies ---
find_package(depthai CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# GLFW: prebuilt zip does not ship a CMake config — use find_library directly
find_library(GLFW_LIB glfw3 PATHS "${GLFW_ROOT}/lib-vc2022" REQUIRED)
set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/include")

# --- ImGui sources (vendored) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# --- Executable ---
add_executable(oak-explorer
    src/main.cpp
    src/oak/Device.cpp
    src/oak/CameraStream.cpp
    src/ui/Sidebar.cpp
    src/ui/StreamView.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(oak-explorer PRIVATE
    src/
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_INCLUDE_DIR}
)

target_link_libraries(oak-explorer PRIVATE
    depthai::core
    ${GLFW_LIB}
    OpenGL::GL
)

# Copy depthai DLLs next to the executable on Windows
# DEPTHAI_WIN64_DIR is set in CMakePresets.json cacheVariables
if(WIN32)
    add_custom_command(TARGET oak-explorer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DEPTHAI_WIN64_DIR}/bin"
            $<TARGET_FILE_DIR:oak-explorer>
        COMMENT "Copying depthai DLLs from ${DEPTHAI_WIN64_DIR}/bin"
    )
endif()
