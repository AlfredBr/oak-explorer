cmake_minimum_required(VERSION 3.20)
project(oak-explorer LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

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

# --- ImPlot sources (vendored) ---
set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/third_party/implot)

# --- Executable ---
add_executable(oak-explorer
    src/main.cpp
    src/oak/Device.cpp
    src/oak/OakStreams.cpp
    src/ui/Sidebar.cpp
    src/ui/StreamView.cpp
    src/ui/DepthView.cpp
    src/ui/StereoView.cpp
    ${IMGUI_SOURCES}
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

if(WIN32)
    target_sources(oak-explorer PRIVATE src/oak-explorer.rc)
endif()

# --- Optional CUDA depth post-processing ---
# nvcc is invoked directly (rather than enable_language(CUDA)) so the build does
# not depend on the CUDA MSBuild integration being installed into Visual Studio.
option(OAK_ENABLE_CUDA "Run depth post-processing on an NVIDIA GPU via CUDA" OFF)
set(OAK_CUDA_ARCH "86" CACHE STRING "CUDA compute capability to build for (86 = RTX 30xx)")
# CUDA 13.x rejects MSVC newer than VS 2022 (14.4x). When the main compiler is newer,
# set this to an installed older toolset (e.g. 14.44); only the .cu object uses it.
set(OAK_CUDA_MSVC_TOOLSET "" CACHE STRING "MSVC toolset version nvcc should use (empty = current environment)")
if(OAK_ENABLE_CUDA)
    set(OAK_CUDA_ENV_SETUP "")
    if(OAK_CUDA_MSVC_TOOLSET)
        # .../VC/Tools/MSVC/<ver>/bin/Hostx64/x64/cl.exe -> .../VC/Auxiliary/Build/vcvars64.bat
        string(REGEX REPLACE "/Tools/MSVC/.*$" "/Auxiliary/Build/vcvars64.bat" OAK_VCVARS "${CMAKE_CXX_COMPILER}")
        file(TO_NATIVE_PATH "${OAK_VCVARS}" OAK_VCVARS)
        # MSBuild already initialised a VS environment; clear it so vcvars re-applies.
        set(OAK_CUDA_ENV_SETUP "set VSCMD_VER=\nset VSCMD_ARG_HOST_ARCH=\nset VSCMD_ARG_TGT_ARCH=\nset VCToolsInstallDir=\nset VCINSTALLDIR=\ncall \"${OAK_VCVARS}\" -vcvars_ver=${OAK_CUDA_MSVC_TOOLSET} >nul 2>nul || exit /b 1")
    endif()
    find_program(NVCC_EXECUTABLE nvcc HINTS "$ENV{CUDA_PATH}/bin" REQUIRED)
    get_filename_component(NVCC_BIN_DIR "${NVCC_EXECUTABLE}" DIRECTORY)
    get_filename_component(CUDA_ROOT "${NVCC_BIN_DIR}" DIRECTORY)
    find_library(CUDART_STATIC_LIB cudart_static PATHS "${CUDA_ROOT}/lib/x64" REQUIRED NO_DEFAULT_PATH)

    # Wrapper script: %1 = CRT flag (/MD or /MDd), %2 = output object
    file(TO_NATIVE_PATH "${NVCC_EXECUTABLE}" OAK_NVCC_NATIVE)
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/src/cuda/DepthProcessor.cu" OAK_CU_NATIVE)
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/src" OAK_SRC_NATIVE)
    set(OAK_NVCC_BAT "${CMAKE_BINARY_DIR}/build_cuda.bat")
    file(WRITE "${OAK_NVCC_BAT}"
"@echo off
${OAK_CUDA_ENV_SETUP}
\"${OAK_NVCC_NATIVE}\" -c \"${OAK_CU_NATIVE}\" -o \"%~2\" -std=c++17 -I \"${OAK_SRC_NATIVE}\" -gencode arch=compute_${OAK_CUDA_ARCH},code=[sm_${OAK_CUDA_ARCH},compute_${OAK_CUDA_ARCH}] -Xcompiler /EHsc,%~1
exit /b %errorlevel%
")

    set(CUDA_OBJ_DIR "${CMAKE_BINARY_DIR}/cuda/$<CONFIG>")
    set(CUDA_OBJ "${CUDA_OBJ_DIR}/DepthProcessor.obj")
    file(TO_NATIVE_PATH "${OAK_NVCC_BAT}" OAK_NVCC_BAT_NATIVE)
    add_custom_command(
        OUTPUT "${CUDA_OBJ}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CUDA_OBJ_DIR}"
        COMMAND cmd /c "${OAK_NVCC_BAT_NATIVE}" "/MD$<$<CONFIG:Debug>:d>" "${CUDA_OBJ}"
        DEPENDS "${CMAKE_SOURCE_DIR}/src/cuda/DepthProcessor.cu"
                "${CMAKE_SOURCE_DIR}/src/cuda/DepthProcessor.h"
        COMMENT "Compiling DepthProcessor.cu with nvcc (sm_${OAK_CUDA_ARCH})"
        VERBATIM
    )
    set_source_files_properties("${CUDA_OBJ}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(oak-explorer PRIVATE "${CUDA_OBJ}")
    target_compile_definitions(oak-explorer PRIVATE OAK_USE_CUDA)
    target_link_libraries(oak-explorer PRIVATE "${CUDART_STATIC_LIB}")
    # cudart_static is built against the static CRT; the rest of the app uses /MD.
    target_link_options(oak-explorer PRIVATE /NODEFAULTLIB:LIBCMT)
endif()

target_include_directories(oak-explorer PRIVATE
    src/
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_INCLUDE_DIR}
    ${IMPLOT_DIR}
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
