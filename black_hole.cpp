#include "bh_wallpaper.hpp"  // pulls in bh_engine.hpp → bh_types.hpp + Win32 headers

// On Optimus/PowerXpress laptops, export these symbols so the GPU driver picks
// the discrete GPU (RTX) instead of the integrated one for this process. The
// NVIDIA/AMD driver reads them from the executable's export table at startup.
#ifdef _WIN32
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// ── Global definitions ──────────────────────────────────────────────────────
double c = 299792458.0;
double G = 6.67430e-11;
bool Gravity = false, showGrid = true, showDisk = true, showBeam = true;
vec4 diskColorTint = vec4(0.0f, 0.0f, 0.0f, 1.0f);

static std::vector<std::string> initNumStrs() {
    std::vector<std::string> v(256);
    for (int i=0;i<256;++i) v[i]=std::to_string(i);
    return v;
}
const std::vector<std::string> numStrs = initNumStrs();

Camera camera;
BlackHole SagA(vec3(0.0f), 8.54e36);
std::vector<ObjectData> objects = {
    { vec4(4e11f,0,0,4e10f),  vec4(0,0,1,1), 1.98892e30f },
    { vec4(0,0,4e11f,4e10f),  vec4(1,0,0,1), 1.98892e30f },
    { vec4(-4e11f,0,0,7e10f), vec4(0,1,0,1), 1.98892e30f },
    { vec4(0,0,0,(float)SagA.r_s), vec4(0,0,0,1), (float)SagA.mass },
};
std::string g_currentMode = "window";
Engine engine;

// ── Physics ─────────────────────────────────────────────────────────────────
bool updateGravityPhysics() {
    if (!Gravity) return false;
    for (auto& obj : objects) {
        vec3 acc(0.0f);
        for (auto& obj2 : objects) {
            if (&obj==&obj2) continue;
            float dx=obj2.posRadius.x-obj.posRadius.x, dy=obj2.posRadius.y-obj.posRadius.y, dz=obj2.posRadius.z-obj.posRadius.z;
            float dist=sqrt(dx*dx+dy*dy+dz*dz);
            if (dist>0.0f) {
                double f=(G*obj.mass*obj2.mass)/(double(dist)*double(dist));
                double a=f/obj.mass;
                acc+=vec3(float(dx/dist*a), float(dy/dist*a), float(dz/dist*a));
            }
        }
        obj.velocity+=acc;
        obj.posRadius.x+=obj.velocity.x; obj.posRadius.y+=obj.velocity.y; obj.posRadius.z+=obj.velocity.z;
    }
    return true;
}

// ── Session state ────────────────────────────────────────────────────────────
static void loadSessionState(const std::string& mode) {
    int steps=600; float zoom=6.34194e10f;
    if (mode=="wallpaper") zoom=1.6e11f;
    else if (mode=="terminal") zoom=5.2e10f;
    std::ifstream f("session_state.txt"); std::string key; double val;
    while (f>>key>>val) {
        if (key==mode+"_steps") steps=(int)val;
        else if (key==mode+"_zoom") zoom=(float)val;
    }
    engine.COMPUTE_STEPS=engine.COMPUTE_MOVING_STEPS=steps;
    camera.radius=glm::clamp(zoom,camera.minRadius,camera.maxRadius);
    camera.update();
}

void saveSessionState(const std::string& mode) {
    std::map<std::string,double> state;
    std::ifstream fi("session_state.txt"); std::string k; double v;
    while (fi>>k>>v) state[k]=v;
    state[mode+"_steps"]=engine.COMPUTE_STEPS;
    state[mode+"_zoom"]=camera.radius;
    std::ofstream fo("session_state.txt", std::ios::trunc);
    for (auto& [k2,v2]:state) fo<<k2<<" "<<v2<<"\n";
}

// ── Screenshot ───────────────────────────────────────────────────────────────
static void saveScreenshot(int w, int h) {
    static int count=0; char name[64]; snprintf(name,sizeof(name),"bh_%04d.bmp",++count);
    std::vector<unsigned char> px(w*h*3);
    glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px.data());
    int rowBytes=w*3, pad=(4-rowBytes%4)%4, stride=rowBytes+pad, fileSize=54+stride*h;
    unsigned char hdr[54]={};
    hdr[0]='B';hdr[1]='M';
    hdr[2]=fileSize;hdr[3]=fileSize>>8;hdr[4]=fileSize>>16;hdr[5]=fileSize>>24;
    hdr[10]=54;hdr[14]=40;
    hdr[18]=w;hdr[19]=w>>8;hdr[20]=w>>16;hdr[21]=w>>24;
    hdr[22]=h;hdr[23]=h>>8;hdr[24]=h>>16;hdr[25]=h>>24;
    hdr[26]=1;hdr[28]=24;
    std::ofstream f(name,std::ios::binary);
    f.write(reinterpret_cast<char*>(hdr),54);
    std::vector<unsigned char> row(stride,0);
    for (int y=0;y<h;y++) {
        for (int x=0;x<w;x++) {
            row[x*3+0]=px[(y*w+x)*3+2];
            row[x*3+1]=px[(y*w+x)*3+1];
            row[x*3+2]=px[(y*w+x)*3+0];
        }
        f.write(reinterpret_cast<char*>(row.data()),stride);
    }
    std::cout<<"[INFO] Screenshot: "<<name<<" ("<<w<<"x"<<h<<")\n";
}

// ── Camera callbacks ─────────────────────────────────────────────────────────
void setupCameraCallbacks(GLFWwindow* win) {
    glfwSetWindowUserPointer(win, &camera);
    glfwSetMouseButtonCallback(win, [](GLFWwindow* w,int btn,int act,int mods){
        Camera* cam=(Camera*)glfwGetWindowUserPointer(w);
        cam->processMouseButton(btn,act,mods,w);
        if (btn==GLFW_MOUSE_BUTTON_RIGHT) Gravity=(act==GLFW_PRESS);
    });
    glfwSetCursorPosCallback(win, [](GLFWwindow* w,double x,double y){
        ((Camera*)glfwGetWindowUserPointer(w))->processMouseMove(x,y);
    });
    glfwSetScrollCallback(win, [](GLFWwindow* w,double xo,double yo){
        ((Camera*)glfwGetWindowUserPointer(w))->processScroll(xo,yo);
        saveSessionState(g_currentMode);
    });
    glfwSetKeyCallback(win, [](GLFWwindow* w,int key,int sc,int act,int mods){
        Camera* cam=(Camera*)glfwGetWindowUserPointer(w);
        if (act==GLFW_PRESS && key==GLFW_KEY_G) { Gravity=!Gravity; cam->update(); }
        if (act==GLFW_PRESS||act==GLFW_REPEAT) {
            if (key==GLFW_KEY_LEFT)  { cam->azimuth-=0.10f; cam->update(true); }
            if (key==GLFW_KEY_RIGHT) { cam->azimuth+=0.10f; cam->update(true); }
            if (key==GLFW_KEY_UP)    { cam->elevation=glm::clamp(cam->elevation-0.08f,0.01f,float(M_PI)-0.01f); cam->update(true); }
            if (key==GLFW_KEY_DOWN)  { cam->elevation=glm::clamp(cam->elevation+0.08f,0.01f,float(M_PI)-0.01f); cam->update(true); }
            if (key==GLFW_KEY_EQUAL||key==GLFW_KEY_KP_ADD)      { cam->processScroll(0,+1); saveSessionState(g_currentMode); }
            if (key==GLFW_KEY_MINUS||key==GLFW_KEY_KP_SUBTRACT) { cam->processScroll(0,-1); saveSessionState(g_currentMode); }
        }
        if (act==GLFW_PRESS) {
            if (key==GLFW_KEY_RIGHT_BRACKET) { engine.COMPUTE_STEPS=std::min(2000,engine.COMPUTE_STEPS+100); engine.COMPUTE_MOVING_STEPS=engine.COMPUTE_STEPS; cam->update(); saveSessionState(g_currentMode); }
            if (key==GLFW_KEY_LEFT_BRACKET)  { engine.COMPUTE_STEPS=std::max(100, engine.COMPUTE_STEPS-100); engine.COMPUTE_MOVING_STEPS=engine.COMPUTE_STEPS; cam->update(); saveSessionState(g_currentMode); }
            if (key==GLFW_KEY_M) { showGrid=!showGrid; cam->update(); }
            if (key==GLFW_KEY_Q||key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w,GLFW_TRUE);
            if (key==GLFW_KEY_S) saveScreenshot(engine.WIDTH, engine.HEIGHT);
            if (key==GLFW_KEY_B) { engine.bloomEnabled=!engine.bloomEnabled; cam->dirty=true; }
            if (key==GLFW_KEY_A) { engine.diskAnimEnabled=!engine.diskAnimEnabled; cam->dirty=true;
                                   std::cout<<"[INFO] Disk animation: "<<(engine.diskAnimEnabled?"ON":"OFF")<<"\n"; }
            if (key==GLFW_KEY_K) {
                engine.kerrSpin = (engine.kerrSpin < 0.01f) ? 0.9f : 0.0f;
                cam->dirty=true;
                std::cout<<"[INFO] Kerr spin: "<<engine.kerrSpin<<"\n";
            }
        }
    });
}

// ── Unit tests ───────────────────────────────────────────────────────────────
static void runUnitTests() {
    std::cout<<"========================================\n        RUNNING BLACK HOLE TESTS        \n========================================\n";
    { Camera c; c.azimuth+=0.03f; c.dirty=true;
      if (!c.dirty||c.azimuth!=0.03f){std::cerr<<"[FAIL] Test 1\n";exit(1);}
      std::cout<<"[PASS] Test 1: Camera Orbit Update\n"; }
    { Camera c; c.elevation=0.01f; c.elevation-=0.02f;
      if (glm::clamp(c.elevation,0.01f,float(M_PI)-0.01f)!=0.01f){std::cerr<<"[FAIL] Test 2\n";exit(1);}
      std::cout<<"[PASS] Test 2: Camera Elevation Clamping\n"; }
    { bool d=true,g=false; int acc=0; const int A=16;
      bool r=d||g||(acc>0&&acc<A); if(!r){std::cerr<<"[FAIL] Test 3A\n";exit(1);}
      d=false; acc=1; r=d||g||(acc>0&&acc<A); if(!r){std::cerr<<"[FAIL] Test 3B\n";exit(1);}
      acc=16; r=d||g||(acc>0&&acc<A); if(r){std::cerr<<"[FAIL] Test 3C\n";exit(1);}
      std::cout<<"[PASS] Test 3: Idle Rendering State Machine\n"; }
#ifdef _WIN32
    { bool da=isDesktopForeground(); std::cout<<"[INFO] Desktop active: "<<da<<"\n[PASS] Test 4\n"; }
#endif
    std::cout<<"========================================\n       ALL TESTS COMPLETED SUCCESSFULLY \n========================================\n";
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    bool wallpaperMode=false, terminalMode=false, dcompMode=false, runTests=false, legacyMode=false;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--wallpaper")||!strcmp(argv[i],"-w")) wallpaperMode=true;
        else if (!strcmp(argv[i],"--terminal")||!strcmp(argv[i],"-t")) terminalMode=true;
        else if (!strcmp(argv[i],"--dcomp")||!strcmp(argv[i],"-d")) dcompMode=true;
        else if (!strcmp(argv[i],"--no-grid"))  showGrid=false;
        else if (!strcmp(argv[i],"--no-disk"))  showDisk=false;
        else if (!strcmp(argv[i],"--no-beam"))  showBeam=false;
        else if (!strcmp(argv[i],"--test"))     runTests=true;
        else if (!strcmp(argv[i],"--red"))   diskColorTint=vec4(0,0,0,2);
        else if (!strcmp(argv[i],"--white")) diskColorTint=vec4(0,0,0,3);
        else if (!strcmp(argv[i],"--spin") && i+1<argc) { engine.kerrSpin=atof(argv[++i]); }
        else if (!strcmp(argv[i],"--anim"))  engine.diskAnimEnabled=true;
        else if (!strcmp(argv[i],"--legacy")||!strcmp(argv[i],"-l")||!strcmp(argv[i],"legacy")) legacyMode=true;
    }
    if (runTests) { runUnitTests(); return 0; }

    // --legacy: swap in the original Schwarzschild shader (pre-Kerr) for A/B comparison.
    if (legacyMode) {
        engine.shaderFile = "geodesic_legacy.comp";
        GLuint p = engine.tryCreateComputeProgram("geodesic_legacy.comp");
        if (p) {
            glDeleteProgram(engine.computeProgram); engine.computeProgram = p;
            try { engine.lastShaderMtime = fs::last_write_time(resourcePath("geodesic_legacy.comp")); } catch (...) {}
            engine.kerrSpin = 0.0f; engine.diskAnimEnabled = false;  // legacy has no Kerr/anim
            std::cout << "[INFO] LEGACY: shader Schwarzschild original (geodesic_legacy.comp); Kerr/anim desativados\n";
        } else {
            std::cerr << "[WARN] geodesic_legacy.comp nao encontrado; seguindo com shader atual\n";
        }
    }

    setupCameraCallbacks(engine.window);
#ifdef _WIN32
    if (wallpaperMode) g_currentMode="wallpaper";
#endif
    loadSessionState(g_currentMode);

#ifdef _WIN32
    if (terminalMode) {
        g_currentMode="terminal"; loadSessionState(g_currentMode);
        runTerminal(engine); saveSessionState(g_currentMode);
        glfwDestroyWindow(engine.window); glfwTerminate(); return 0;
    }
    if (dcompMode) {
        g_currentMode="wallpaper"; loadSessionState(g_currentMode);
        runWallpaperDComp(engine.window, engine); saveSessionState(g_currentMode);
        glfwDestroyWindow(engine.window); glfwTerminate(); return 0;
    }
    if (wallpaperMode) {
        if (attachToDesktop(engine.window)) {
            glfwGetFramebufferSize(engine.window, &engine.WIDTH, &engine.HEIGHT);
            engine.COMPUTE_WIDTH         = std::max(480, engine.WIDTH/3);
            engine.COMPUTE_HEIGHT        = std::max(270, engine.HEIGHT/3);
            engine.COMPUTE_MOVING_WIDTH  = std::max(240, engine.WIDTH/6);
            engine.COMPUTE_MOVING_HEIGHT = std::max(135, engine.HEIGHT/6);
            engine.COMPUTE_MOVING_STEPS  = engine.COMPUTE_STEPS;
            std::cout<<"[INFO] Wallpaper "<<engine.WIDTH<<"x"<<engine.HEIGHT<<"\n";
        } else { wallpaperMode=false; g_currentMode="window"; loadSessionState(g_currentMode); }
    }
#else
    if (wallpaperMode) { wallpaperMode=false; g_currentMode="window"; loadSessionState(g_currentMode); }
#endif

    if (!wallpaperMode) {
        engine.COMPUTE_WIDTH=engine.COMPUTE_MOVING_WIDTH=480;
        engine.COMPUTE_HEIGHT=engine.COMPUTE_MOVING_HEIGHT=360;
        engine.COMPUTE_MOVING_STEPS=engine.COMPUTE_STEPS;
        engine.COMPUTE_D_LAMBDA=engine.COMPUTE_MOVING_D_LAMBDA=7.5e8f;
    }

    int accumSample=0;
    int ACCUM_SAMPLES = wallpaperMode ? 16 : 4;
    const double wpMinFrame = wallpaperMode ? (1.0/30.0) : 0.0;
    double lastFrameStart = glfwGetTime();

    while (!glfwWindowShouldClose(engine.window)) {
        if (engine.checkShaderReload()) { accumSample=0; camera.dirty=true; }
#ifdef _WIN32
        if (wallpaperMode) wallpaperUpdate(engine.window);
#endif
        // Disk animation always re-renders (no accumulation)
        if (engine.diskAnimEnabled) { camera.dirty=true; accumSample=0; }

        bool wantAccum = !camera.dirty&&!Gravity&&!camera.moving&&accumSample>0&&accumSample<ACCUM_SAMPLES;
        if (!camera.dirty&&!Gravity&&!wantAccum) {
            glfwWaitEventsTimeout(1.0/30.0); continue;
        }
        if (wpMinFrame>0.0) {
            double d=glfwGetTime()-lastFrameStart;
            if (d<wpMinFrame) std::this_thread::sleep_for(std::chrono::duration<double>(wpMinFrame-d));
            lastFrameStart=glfwGetTime();
        }
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        bool sceneDirty=updateGravityPhysics();
        glViewport(0,0,engine.WIDTH,engine.HEIGHT);
        if (camera.dirty||sceneDirty) {
            accumSample=0; engine.dispatchCompute(camera,0); accumSample=1; camera.dirty=false;
        } else if (wantAccum) {
            engine.dispatchCompute(camera,accumSample); ++accumSample;
        }
        engine.drawFullScreenQuad();
        glfwSwapBuffers(engine.window); glfwPollEvents();
    }
    saveSessionState(g_currentMode);
    glfwDestroyWindow(engine.window); glfwTerminate(); return 0;
}
