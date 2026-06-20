#pragma once
#ifdef _WIN32
#include "bh_engine.hpp"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

struct WallpaperState {
    int monW = 0, monH = 0;
    float baseAzimuth = 0.0f, baseElevation = float(M_PI)/2.0f;
    float offAz = 0.0f, offEl = 0.0f;
    float amplitudeAz = 0.45f, amplitudeEl = 0.28f;
};
extern WallpaperState wallpaper;
extern HWND g_wpHost;
extern int  g_wpMode;

bool isDesktopForeground();
bool attachToDesktop(GLFWwindow* win);
void wallpaperUpdate(GLFWwindow* win);
void runWallpaperDComp(GLFWwindow* win, Engine& eng);
void runTerminal(Engine& eng);
#endif // _WIN32
