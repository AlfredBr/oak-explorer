// src/main.cpp
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <implot.h>

#include "oak/Device.h"
#include "oak/OakStreams.h"
#include "ui/Sidebar.h"
#include "ui/StreamView.h"
#include "ui/DepthView.h"
#include "ui/StereoView.h"
#include "ui/SpatialMode.h"

#include <cstdio>
#include <chrono>
#include <string>
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "resource.h"

// Returns the directory containing this executable (no trailing slash).
static std::string exeDir() {
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0) return ".";
    std::string s(buf, len);
    size_t slash = s.rfind('\\');
    return (slash != std::string::npos) ? s.substr(0, slash) : ".";
}

// GLFW window geometry persisted next to the exe in window.ini.
struct WindowGeom { int x = 100, y = 100, w = 1536, h = 864; };

static WindowGeom loadWindowGeom(const std::string& path) {
    WindowGeom g;
    FILE* f = fopen(path.c_str(), "r");
    if (f) { fscanf(f, "%d %d %d %d", &g.x, &g.y, &g.w, &g.h); fclose(f); }
    return g;
}

static void saveWindowGeom(const std::string& path, GLFWwindow* win) {
    int x, y, w, h;
    glfwGetWindowPos(win, &x, &y);
    glfwGetWindowSize(win, &w, &h);
    FILE* f = fopen(path.c_str(), "w");
    if (f) { fprintf(f, "%d %d %d %d\n", x, y, w, h); fclose(f); }
}

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static void setWindowIcons(GLFWwindow* window, HICON& smallIcon, HICON& largeIcon) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    smallIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    largeIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));

    if (!smallIcon || !largeIcon) {
        fprintf(stderr, "Failed to load the application icon resource.\n");
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
}

int main() {
    const std::string dir      = exeDir();
    const std::string iniPath  = dir + "\\imgui.ini";
    const std::string winPath  = dir + "\\window.ini";

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return 1;

    // OpenGL 3.3 core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    WindowGeom geom = loadWindowGeom(winPath);
    GLFWwindow* window = glfwCreateWindow(geom.w, geom.h, "oak-explorer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwSetWindowPos(window, geom.x, geom.y);

    HICON smallIcon = nullptr;
    HICON largeIcon = nullptr;
    setWindowIcons(window, smallIcon, largeIcon);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();    // must be after ImGui::CreateContext()
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = iniPath.c_str();   // persist next to exe, not cwd
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // OpenGL context is now active — safe to create GL resources
    oak::OakDevice  device;
    oak::OakStreams  streams;
    auto nextStreamAttempt = std::chrono::steady_clock::now();
    constexpr auto streamRetryInterval = std::chrono::seconds(2);

    ui::SpatialMode spatialMode = ui::SpatialMode::Lock;  // default: click to lock

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Retry the pipeline while offline; throttle retries to avoid repeatedly
        // rebuilding a pipeline when no camera is connected or available.
        if (!streams.isStreaming()) {
            device.poll();
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextStreamAttempt) {
                streams.open();
                nextStreamAttempt = std::chrono::steady_clock::now() + streamRetryInterval;
            }
        }
        streams.poll();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode);

        // Render UI panels
        ui::renderSidebar(device, streams, spatialMode);
        ui::renderStreamView(streams, spatialMode);
        ui::renderDepthView(streams);
        ui::renderStereoView(streams);

        // Render and present
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Save window geometry before teardown
    saveWindowGeom(winPath, window);

    // Cleanup — GL resources must be freed while context is still active
    streams.close();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();   // before ImGui::DestroyContext()
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    if (smallIcon) DestroyIcon(smallIcon);
    if (largeIcon) DestroyIcon(largeIcon);
    glfwTerminate();
    return 0;
}
