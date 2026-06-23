#ifdef _WIN32
#include "bh_wallpaper.hpp"
#include <sstream>
#include <fstream>

WallpaperState wallpaper;
HWND g_wpHost = nullptr;
int  g_wpMode = 0;

static std::string hwndHex(HWND h) { std::ostringstream o; o << (void*)h; return o.str(); }

static void wlog(const std::string& s) {
    static std::ofstream f("wallpaper.log", std::ios::trunc);
    f << s << "\n"; f.flush();
    std::cerr << "[wallpaper] " << s << "\n";
}

bool isDesktopForeground() {
    HWND fg = GetForegroundWindow(); if (!fg) return false;
    char cls[256];
    if (GetClassNameA(fg, cls, sizeof(cls))) {
        std::string c(cls);
        if (c=="Progman"||c=="WorkerW"||c=="Shell_TrayWnd"||c=="SysListView32"||c=="SHELLDLL_DefView")
            return true;
    }
    return fg==GetShellWindow() || fg==GetDesktopWindow();
}

bool attachToDesktop(GLFWwindow* win) {
    wallpaper.monW = GetSystemMetrics(SM_CXSCREEN);
    wallpaper.monH = GetSystemMetrics(SM_CYSCREEN);
    if (wallpaper.monW<=0 || wallpaper.monH<=0) return false;
    HWND hwnd = glfwGetWin32Window(win);
    wlog("Bottom-most wallpaper " + std::to_string(wallpaper.monW) + "x" + std::to_string(wallpaper.monH));
    glfwSetWindowAttrib(win, GLFW_DECORATED, GLFW_FALSE);
    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
    style = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;
    SetWindowLongPtrA(hwnd, GWL_STYLE, style);
    LONG_PTR ex = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, ex);
    glfwSetWindowPos(win, 0, 0);
    glfwSetWindowSize(win, wallpaper.monW, wallpaper.monH);
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, wallpaper.monW, wallpaper.monH,
                 SWP_NOACTIVATE|SWP_SHOWWINDOW|SWP_FRAMECHANGED);
    g_wpHost = nullptr; g_wpMode = 1;
    wallpaper.baseAzimuth = camera.azimuth;
    wallpaper.baseElevation = camera.elevation;
    return true;
}

void wallpaperUpdate(GLFWwindow* win) {
    if ((GetAsyncKeyState(VK_CONTROL)&0x8000) && (GetAsyncKeyState(VK_MENU)&0x8000) &&
        (GetAsyncKeyState('Q')&0x8000)) { glfwSetWindowShouldClose(win, GLFW_TRUE); return; }

    if (g_wpMode==1) {
        static int z=0;
        if ((++z%60)==0) SetWindowPos(glfwGetWin32Window(win), HWND_BOTTOM, 0,0,0,0,
                                       SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }

    POINT p; if (!GetCursorPos(&p)) return;
    float nx = glm::clamp(2.0f*float(p.x)/float(wallpaper.monW)-1.0f,-1.0f,1.0f);
    float ny = glm::clamp(2.0f*float(p.y)/float(wallpaper.monH)-1.0f,-1.0f,1.0f);
    float taz = nx*wallpaper.amplitudeAz, tel = ny*wallpaper.amplitudeEl;
    const float ease = 0.12f;
    float daz = (taz-wallpaper.offAz)*ease, del = (tel-wallpaper.offEl)*ease;
    bool mouseMoving = (fabsf(daz)>1e-4f||fabsf(del)>1e-4f);
    if (mouseMoving) { wallpaper.offAz+=daz; wallpaper.offEl+=del; }

    bool ctrlAlt = (GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000);
    bool desktopActive = isDesktopForeground();
    bool keyInteract = false;

    if (desktopActive||ctrlAlt) {
        if (GetAsyncKeyState(VK_LEFT)&0x8000)  { wallpaper.baseAzimuth-=0.03f; keyInteract=true; }
        if (GetAsyncKeyState(VK_RIGHT)&0x8000) { wallpaper.baseAzimuth+=0.03f; keyInteract=true; }
        if (GetAsyncKeyState(VK_UP)&0x8000)    { wallpaper.baseElevation=glm::clamp(wallpaper.baseElevation-0.02f,0.01f,float(M_PI)-0.01f); keyInteract=true; }
        if (GetAsyncKeyState(VK_DOWN)&0x8000)  { wallpaper.baseElevation=glm::clamp(wallpaper.baseElevation+0.02f,0.01f,float(M_PI)-0.01f); keyInteract=true; }
        if ((GetAsyncKeyState(VK_OEM_PLUS)&0x8000)||(GetAsyncKeyState(VK_ADD)&0x8000))  { camera.processScroll(0,+1); keyInteract=true; saveSessionState("wallpaper"); }
        if ((GetAsyncKeyState(VK_OEM_MINUS)&0x8000)||(GetAsyncKeyState(VK_SUBTRACT)&0x8000)) { camera.processScroll(0,-1); keyInteract=true; saveSessionState("wallpaper"); }

        static bool lbWas=false; bool lbIs=(GetAsyncKeyState(VK_OEM_4)&0x8000)!=0;
        if (lbIs&&!lbWas){ engine.COMPUTE_STEPS=std::max(100,engine.COMPUTE_STEPS-100); engine.COMPUTE_MOVING_STEPS=engine.COMPUTE_STEPS; keyInteract=true; saveSessionState("wallpaper"); }
        lbWas=lbIs;
        static bool rbWas=false; bool rbIs=(GetAsyncKeyState(VK_OEM_6)&0x8000)!=0;
        if (rbIs&&!rbWas){ engine.COMPUTE_STEPS=std::min(2000,engine.COMPUTE_STEPS+100); engine.COMPUTE_MOVING_STEPS=engine.COMPUTE_STEPS; keyInteract=true; saveSessionState("wallpaper"); }
        rbWas=rbIs;
        static bool gWas=false; bool gIs=(GetAsyncKeyState('G')&0x8000)!=0;
        if (gIs&&!gWas){ Gravity=!Gravity; keyInteract=true; } gWas=gIs;
        static bool mWas=false; bool mIs=(GetAsyncKeyState('M')&0x8000)!=0;
        if (mIs&&!mWas){ showGrid=!showGrid; keyInteract=true; } mWas=mIs;
        // Kerr spin (K toggle, .,/ fine) and disk animation (A) — compute uniforms,
        // so they take effect in the wallpaper immediately.
        static bool kWas=false; bool kIs=(GetAsyncKeyState('K')&0x8000)!=0;
        if (kIs&&!kWas){ engine.kerrSpin=(engine.kerrSpin<0.01f)?kKerrSpinOn:0.0f; keyInteract=true; saveSessionState("wallpaper"); } kWas=kIs;
        static bool aWas=false; bool aIs=(GetAsyncKeyState('A')&0x8000)!=0;
        if (aIs&&!aWas){ engine.diskAnimEnabled=!engine.diskAnimEnabled; keyInteract=true; } aWas=aIs;
        static bool pdWas=false; bool pdIs=(GetAsyncKeyState(VK_OEM_PERIOD)&0x8000)!=0;
        if (pdIs&&!pdWas){ engine.kerrSpin=glm::clamp(engine.kerrSpin+kKerrSpinStep,0.0f,1.0f); keyInteract=true; saveSessionState("wallpaper"); } pdWas=pdIs;
        static bool cmWas=false; bool cmIs=(GetAsyncKeyState(VK_OEM_COMMA)&0x8000)!=0;
        if (cmIs&&!cmWas){ engine.kerrSpin=glm::clamp(engine.kerrSpin-kKerrSpinStep,0.0f,1.0f); keyInteract=true; saveSessionState("wallpaper"); } cmWas=cmIs;
        static bool tWas=false; bool tIs=(GetAsyncKeyState('T')&0x8000)!=0;
        if (tIs&&!tWas){ bool sh=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0; engine.bhTilt += sh?-kTiltStep:kTiltStep; keyInteract=true; } tWas=tIs;
        // F: cycle the scene palette (disk + jets + meteors all follow it)
        static bool fWas=false; bool fIs=(GetAsyncKeyState('F')&0x8000)!=0;
        if (fIs&&!fWas){ cycleColorMode(); keyInteract=true; } fWas=fIs;
    }

    if (g_cinematic) { cinematicCamera(glfwGetTime()); return; }   // scripted fly-through

    if (mouseMoving||keyInteract) {
        camera.azimuth = wallpaper.baseAzimuth+wallpaper.offAz;
        camera.elevation = glm::clamp(wallpaper.baseElevation+wallpaper.offEl,0.01f,float(M_PI)-0.01f);
        camera.moving = true; camera.dirty = true;
    } else {
        wallpaper.baseAzimuth += 0.003f;
        camera.azimuth = wallpaper.baseAzimuth+wallpaper.offAz;
        camera.elevation = glm::clamp(wallpaper.baseElevation+wallpaper.offEl,0.01f,float(M_PI)-0.01f);
        camera.moving = false; camera.dirty = true;
    }
}

void runWallpaperDComp(GLFWwindow* win, Engine& eng) {
    glfwHideWindow(win);
    int W = GetSystemMetrics(SM_CXSCREEN), H = GetSystemMetrics(SM_CYSCREEN);
    wallpaper.monW = W; wallpaper.monH = H;
    // Render resolution = screen / wpDiv (DComp bilinear-upscales to full screen).
    // Was W/5 (very soft, 5x upscale); default now W/2 for a sharper wallpaper.
    // The wallpaper auto-rotates every frame so TAA never accumulates — base
    // resolution drives sharpness. Lower wpDiv = sharper but heavier (rotation can
    // get choppy on a modest GPU); raise BH_WP_DIV to smooth it back.
    int wpDiv = 2;
    if (const char* d = std::getenv("BH_WP_DIV")) { int v = std::atoi(d); if (v>=1 && v<=8) wpDiv = v; }
    int rw = std::max(480, W/wpDiv), rh = std::max(270, H/wpDiv);
    eng.COMPUTE_WIDTH = eng.COMPUTE_MOVING_WIDTH = rw;
    eng.COMPUTE_HEIGHT = eng.COMPUTE_MOVING_HEIGHT = rh;
    eng.COMPUTE_MOVING_STEPS = eng.COMPUTE_STEPS;

    HWND progman = FindWindowA("Progman", nullptr);
    DWORD_PTR ig=0;
    if (progman) {
        SendMessageTimeoutA(progman,0x052C,0xD,0x1,SMTO_NORMAL,1000,&ig);
        SendMessageTimeoutA(progman,0x052C,0,0,SMTO_NORMAL,1000,&ig);
    }
    HWND workerw = progman ? FindWindowExA(progman,nullptr,"WorkerW",nullptr) : nullptr;
    if (!workerw) workerw = progman;

    WNDCLASSA wc={}; wc.lpfnWndProc=DefWindowProcA;
    wc.hInstance=GetModuleHandleA(nullptr); wc.lpszClassName="BHDComp";
    RegisterClassA(&wc);
    HWND host = CreateWindowExA(WS_EX_NOREDIRECTIONBITMAP,"BHDComp","",WS_POPUP,
                                0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);
    ShowWindow(host, SW_SHOWNOACTIVATE);

    ID3D11Device* d3d=nullptr; ID3D11DeviceContext* ctx=nullptr;
    if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,nullptr,&ctx)))
    { wlog("DComp: D3D11 failed"); return; }

    IDXGIDevice* dxgiDev=nullptr; d3d->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgiDev);
    IDXGIAdapter* ad=nullptr; dxgiDev->GetAdapter(&ad);
    IDXGIFactory2* fac=nullptr; ad->GetParent(__uuidof(IDXGIFactory2),(void**)&fac);

    DXGI_SWAP_CHAIN_DESC1 scd={}; scd.Width=rw; scd.Height=rh;
    scd.Format=DXGI_FORMAT_B8G8R8A8_UNORM; scd.BufferCount=2; scd.SampleDesc.Count=1;
    scd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; scd.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    IDXGISwapChain1* sc=nullptr; fac->CreateSwapChainForComposition(d3d,&scd,nullptr,&sc);

    D3D11_TEXTURE2D_DESC td={}; td.Width=rw; td.Height=rh; td.MipLevels=1; td.ArraySize=1;
    td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DYNAMIC;
    td.BindFlags=D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    ID3D11Texture2D* staging=nullptr; d3d->CreateTexture2D(&td,nullptr,&staging);

    IDCompositionDevice* dc=nullptr;
    DCompositionCreateDevice(dxgiDev,__uuidof(IDCompositionDevice),(void**)&dc);
    IDCompositionTarget* tg=nullptr; dc->CreateTargetForHwnd(host,TRUE,&tg);
    IDCompositionVisual* vis=nullptr; dc->CreateVisual(&vis); vis->SetContent(sc);
    IDCompositionScaleTransform* scale=nullptr; dc->CreateScaleTransform(&scale);
    scale->SetScaleX(float(W)/rw); scale->SetScaleY(float(H)/rh);
    vis->SetTransform(scale); tg->SetRoot(vis); dc->Commit();

    LONG_PTR st=GetWindowLongPtrA(host,GWL_STYLE); st|=WS_CHILD;
    SetWindowLongPtrA(host,GWL_STYLE,st);
    SetParent(host,workerw);
    SetWindowPos(host,nullptr,0,0,W,H,SWP_NOACTIVATE|SWP_SHOWWINDOW|SWP_FRAMECHANGED);

    g_wpMode=3; wallpaper.baseAzimuth=camera.azimuth; wallpaper.baseElevation=camera.elevation;
    wlog("DComp wallpaper behind icons " + std::to_string(rw) + "x" + std::to_string(rh));

    // Offscreen target to composite compute + bloom (toggle B) before the readback.
    // The DComp path reads a texture rather than the window, so it otherwise skips
    // the bloom pass that drawFullScreenQuad does in the windowed path.
    GLuint wpFbo=0, wpTex=0;
    glGenTextures(1,&wpTex); glBindTexture(GL_TEXTURE_2D,wpTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,rw,rh,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenFramebuffers(1,&wpFbo); glBindFramebuffer(GL_FRAMEBUFFER,wpFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,wpTex,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);

    int accumSample=0; const int ACCUM=16;
    const double minFrame=1.0/30.0; double last=glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        if ((GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)&&
            (GetAsyncKeyState('Q')&0x8000)) break;
        wallpaperUpdate(win);
        bool sceneDirty = updateGravityPhysics();
        bool wantAccum = !camera.dirty&&!Gravity&&!camera.moving&&accumSample>0&&accumSample<ACCUM;
        if (!camera.dirty&&!sceneDirty&&!wantAccum) {
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            continue;
        }
        if (camera.dirty||sceneDirty) { accumSample=0; eng.dispatchCompute(camera,0); accumSample=1; camera.dirty=false; }
        else if (wantAccum) { eng.dispatchCompute(camera,accumSample); ++accumSample; }

        // composite compute + bloom into wpTex, then read that (bloomed) texture
        glBindFramebuffer(GL_FRAMEBUFFER, wpFbo);
        glViewport(0,0,rw,rh);
        eng.drawFullScreenQuad();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, wpTex);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
        eng.initPBO(size_t(rw)*rh*4);
        const unsigned char* pboPtr = eng.pboBeginRead(GL_BGRA);
        if (pboPtr) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_WRITE_DISCARD,0,&ms))) {
                for (int y=0;y<rh;++y)
                    memcpy((unsigned char*)ms.pData+size_t(y)*ms.RowPitch,
                           pboPtr+size_t(rh-1-y)*rw*4, size_t(rw)*4);
                ctx->Unmap(staging,0);
            }
        }
        eng.pboEndRead();
        ID3D11Texture2D* back=nullptr; sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&back);
        if (back){ ctx->CopyResource(back,staging); back->Release(); }
        sc->Present(1,0); dc->Commit();

        glfwPollEvents();
        double dt=glfwGetTime()-last;
        if (dt<minFrame) std::this_thread::sleep_for(std::chrono::duration<double>(minFrame-dt));
        last=glfwGetTime();
    }
}
#endif // _WIN32
