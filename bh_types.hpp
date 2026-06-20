#pragma once
#ifdef _WIN32
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
// Include full <windows.h> here — BEFORE <glm> (which pulls in std::byte via
// <cstddef>) and BEFORE `using namespace std;` below. This guarantees every
// Win32/COM header that uses an unqualified `byte` (rpcndr.h, objidl.h, …) is
// parsed while only the global ::byte is visible, avoiding the std::byte
// ambiguity. It also makes MAX_PATH/DWORD/GetModuleFileName/console APIs
// available to resourcePath() and the terminal/engine modules.
#include <windows.h>
#endif
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <map>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace glm;
using namespace std;
namespace fs = std::filesystem;
using Clock = std::chrono::high_resolution_clock;

static std::string resourcePath(const char* name) {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::string s(path, n);
        size_t slash = s.find_last_of("\\/");
        if (slash != std::string::npos) return s.substr(0, slash + 1) + name;
    }
#endif
    return std::string(name);
}

static float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) { f /= float(base); r += f * float(index % base); index /= base; }
    return r;
}

struct Camera {
    vec3 target = vec3(0.0f, 0.0f, 0.0f);
    float radius = 6.34194e10f;
    float minRadius = 1e10f, maxRadius = 1e12f;
    float azimuth = 0.0f;
    float elevation = M_PI / 2.0f;
    float orbitSpeed = 0.01f;
    float panSpeed = 0.01f;
    double zoomSpeed = 25e9f;
    bool dragging = false, panning = false, moving = false, dirty = true;
    double lastX = 0.0, lastY = 0.0;

    vec3 position() const {
        float e = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
        return vec3(radius*sin(e)*cos(azimuth), radius*cos(e), radius*sin(e)*sin(azimuth));
    }
    void update(bool transientMove = false) {
        target = vec3(0.0f); dirty = true;
        moving = transientMove || dragging || panning;
    }
    void processMouseMove(double x, double y) {
        float dx = float(x - lastX), dy = float(y - lastY);
        if (dragging && !panning) {
            azimuth   += dx * orbitSpeed;
            elevation  = glm::clamp(elevation - dy*orbitSpeed, 0.01f, float(M_PI)-0.01f);
        }
        lastX = x; lastY = y; update();
    }
    void processMouseButton(int button, int action, int mods, GLFWwindow* win) {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (action == GLFW_PRESS)   { dragging = true; panning = false; glfwGetCursorPos(win, &lastX, &lastY); }
            else if (action == GLFW_RELEASE) { dragging = false; panning = false; }
        }
        update();
    }
    void processScroll(double, double yoffset) {
        radius = glm::clamp(float(radius - yoffset*zoomSpeed), minRadius, maxRadius);
        update(true);
    }
};

struct BlackHole {
    vec3 position; double mass, r_s;
    BlackHole(vec3 pos, double m) : position(pos), mass(m) {
        constexpr double G = 6.67430e-11, c = 299792458.0;
        r_s = 2.0 * G * m / (c * c);
    }
};

struct ObjectData {
    vec4 posRadius;
    vec4 color;
    float  mass;
    vec3 velocity = vec3(0.0f);
};

// --- Cross-module globals (defined in black_hole.cpp) ---
extern double c, G;
extern bool Gravity, showGrid, showDisk, showBeam;
extern vec4 diskColorTint;
extern const std::vector<std::string> numStrs;
extern Camera camera;
extern BlackHole SagA;
extern std::vector<ObjectData> objects;
extern std::string g_currentMode;
extern bool updateGravityPhysics();
extern void saveSessionState(const std::string& mode);
