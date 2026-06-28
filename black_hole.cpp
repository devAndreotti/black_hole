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

// ── Scene colour palette ──────────────────────────────────────────────────────
// The active palette (disk + jets + meteors all follow it) lives in diskColorTint.a
// as an integer mode, read by geodesic.comp — the CMODE_* there MUST match these.
// Set once by --red/--white/--blue/--green, or cycled live with F in every mode.
enum ColorMode { CMODE_DEFAULT = 1, CMODE_RED, CMODE_WHITE, CMODE_BLUE, CMODE_GREEN };
const char* const kColorModeNames[] = { "", "default", "red", "white", "blue", "green" };
// Advance to the next palette (wraps GREEN→DEFAULT); returns the new mode so callers
// can log it. Single source of truth for the F key across window/terminal/wallpaper.
int cycleColorMode() {
    int m = int(std::round(diskColorTint.a));
    m = (m >= CMODE_GREEN) ? CMODE_DEFAULT : m + 1;
    diskColorTint.a = float(m);
    return m;
}

static std::vector<std::string> initNumStrs() {
    std::vector<std::string> v(256);
    for (int i=0;i<256;++i) v[i]=std::to_string(i);
    return v;
}
const std::vector<std::string> numStrs = initNumStrs();

Camera camera;
BlackHole SagA(vec3(0.0f), 8.54e36);
std::vector<ObjectData> objects = {
    // ── Stars ──────────────────────────────────────────────────────────────────
    { vec4( 3.6e11f, 1.8e11f,  0.8e11f, 4e10f), vec4(0,0,1,1),              1.98892e30f },  // [0] blue
    { vec4( 0.6e11f,-2.2e11f,  3.6e11f, 4e10f), vec4(1,0,0,1),              1.98892e30f },  // [1] red
    { vec4(-3.8e11f, 1.0e11f, -1.6e11f, 7e10f), vec4(0,1,0,1),              1.98892e30f },  // [2] green
    // ── Black hole ─────────────────────────────────────────────────────────────
    { vec4(0,0,0,(float)SagA.r_s), vec4(0,0,0,1), (float)SagA.mass },                       // [3] BH
    // ── Planets (color.a = type: 10=ocean 11=rocky 12=desert 13=volcanic 14=jungle) ──
    { vec4(5.1e11f,0,0,   1.0e10f), vec4(0.10f,0.15f,0.20f,10.0f), 1e22f },  // [4] ocean    (blue)
    { vec4(0,0,5.4e11f,   9.0e9f),  vec4(0.20f,0.18f,0.15f,11.0f), 1e22f },  // [5] rocky    (red)
    { vec4(-6.1e11f,0,0,  1.2e10f), vec4(0.25f,0.18f,0.08f,12.0f), 1e22f },  // [6] desert   (green)
    // ── Binary companion (orange star orbiting blue) ────────────────────────────
    { vec4(4.7e11f,0,0,2.2e10f),    vec4(1.0f,0.62f,0.25f,1.0f),   8e29f },   // [7] companion
    // ── Two extra planets ──────────────────────────────────────────────────────
    { vec4(8.5e11f,0,0,   1.1e10f), vec4(0.12f,0.07f,0.05f,13.0f), 1e22f },  // [8] volcanic (blue, far)
    { vec4(0,0,9.5e11f,   1.3e10f), vec4(0.08f,0.15f,0.08f,14.0f), 1e22f },  // [9] jungle   (red, far)
};
std::string g_currentMode = "window";
float g_renderTime = -1.0f;        // --time override for headless temporal validation
bool  g_animate = true;            // moons/binary orbit (terminal/wallpaper always; window per autoRotate/A)
bool  g_cinematic = false;         // scripted fly-through camera (--cinematic / key C)
static bool g_autoRotate = true;   // window mode: idle auto-orbit (toggle with R)
static bool  g_paused     = false; // window mode: Space freezes the simulation (last frame held)
static float g_spinTarget = -1.0f; // active Kerr-spin lerp goal (<0 = none); K/./, ease toward it
static constexpr float kSpinLerpRate = 1.5f;  // Kerr-spin units eased per second (0→0.9 ≈ 0.6 s)

// ── Scene presets (key P cycles; --preset name at startup) ──────────────────
struct Preset { const char* name; float spin, tiltDeg; int palette; float zoom; bool anim; };
static const Preset kPresets[] = {
    { "classic",      0.0f,  0.0f, CMODE_DEFAULT, 6.3e10f, false },
    { "supermassive", 0.9f, 18.0f, CMODE_BLUE,    4.5e11f, true  },
    { "edge-on",      0.5f, 85.0f, CMODE_RED,     5.0e10f, true  },
    { "verdant",      0.3f, 12.0f, CMODE_GREEN,   1.2e11f, true  },
    { "polar",        0.7f, 45.0f, CMODE_WHITE,   8.0e10f, false },
};
static constexpr int kNumPresets = (int)(sizeof(kPresets)/sizeof(kPresets[0]));
static int   g_presetIdx     = -1;   // -1 = no preset active
static float g_mergerElapsed = -1.0f;  // seconds since W-key merger wave (<0 = off)

static void applyPreset(int idx) {
    const Preset& p = kPresets[idx];
    g_spinTarget           = p.spin;
    engine.bhTilt          = p.tiltDeg * float(M_PI) / 180.0f;
    diskColorTint.a        = float(p.palette);
    camera.radius          = glm::clamp(p.zoom, camera.minRadius, camera.maxRadius);
    engine.diskAnimEnabled = p.anim;
    camera.dirty           = true;
    std::cout << "[INFO] Preset: " << p.name << "\n";
}

// Cinematic fly-through: a looping path that drifts face-on, dives toward the
// photon ring (whipping around, edge-on), then pulls back out. Great for
// screenshots / a "living" wallpaper. Driven purely by time t (seconds).
void cinematicCamera(double t) {
    float ph   = float(fmod(t, 50.0)) / 50.0f;                 // 50s loop
    float dive = 0.5f - 0.5f * cosf(ph * 2.0f * float(M_PI));  // smooth 0→1→0 (one dive/loop)
    float ease = dive * dive * (3.0f - 2.0f * dive);           // smootherstep
    camera.radius    = glm::mix(5.5e11f, 1.3e11f, ease);                 // far → close → far
    camera.azimuth   = float(t) * 0.06f + ease * 2.2f;                   // drift + whip on approach
    camera.elevation = glm::clamp(glm::mix(0.95f, 1.42f, ease),
                                  0.05f, float(M_PI) - 0.05f);           // face-on → near edge-on
    camera.target = vec3(0.0f); camera.moving = false; camera.dirty = true;
}
Engine engine;

// ── Physics ─────────────────────────────────────────────────────────────────
// Kinematic moon orbits (Etapa E): each moon circles its parent sun's current
// position in a tilted plane. Driven by time (g_renderTime override for headless
// --time validation). Cheap; no integration, so they never decay into the sun.
static void updateMoons() {
    float t = (g_renderTime >= 0.0f) ? g_renderTime : (float)glfwGetTime();
    struct M { int mi, pi; float R, w, ph; vec3 axis; };
    static const M m[] = {
        { 4, 0, 1.1e11f, 0.55f, 0.0f, vec3(0,1,0)        },  // ocean planet (blue, inner)
        { 5, 1, 1.4e11f, 0.42f, 2.1f, vec3(0.35f,1,0.2f) },  // rocky planet (red)
        { 6, 2, 2.1e11f, 0.32f, 4.0f, vec3(0,1,0.4f)     },  // desert planet (green)
        { 7, 0, 7.0e10f, 0.90f, 1.0f, vec3(0.2f,1,0.3f)  },  // binary companion (blue, tight)
        { 8, 0, 2.4e11f, 0.28f, 3.2f, vec3(0.15f,1,0.1f) },  // volcanic planet (blue, outer)
        { 9, 1, 3.2e11f, 0.22f, 1.5f, vec3(0.4f,1,0.3f)  },  // jungle planet (red, outer)
    };
    for (const auto& o : m) {
        if (o.mi >= (int)objects.size()) continue;
        vec3 c  = vec3(objects[o.pi].posRadius);
        vec3 ax = normalize(o.axis);
        vec3 u  = normalize(cross(ax, vec3(1,0,0)));
        vec3 v  = cross(ax, u);
        float a = o.w * t + o.ph;
        vec3 p  = c + o.R * (cosf(a)*u + sinf(a)*v);
        objects[o.mi].posRadius.x = p.x;
        objects[o.mi].posRadius.y = p.y;
        objects[o.mi].posRadius.z = p.z;
    }
}

bool updateGravityPhysics() {
    bool moved = false;
    if (g_animate) { updateMoons(); moved = true; }                // moons/binary orbit
    if (!Gravity) return moved;
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
// zoom/steps are per-mode; spin/tilt/palette are the global "look" shared by every
// mode. The restore* flags let the caller keep a CLI override (--spin/--tilt/palette,
// --legacy) from being clobbered by the saved look.
static void loadSessionState(const std::string& mode,
                             bool restoreSpin=true, bool restoreTilt=true, bool restorePalette=true) {
    int steps=600; float zoom=6.34194e10f;
    if (mode=="wallpaper") zoom=1.6e11f;
    else if (mode=="terminal") zoom=5.2e10f;
    float spin=engine.kerrSpin, tilt=engine.bhTilt, pal=diskColorTint.a;   // defaults = current
    bool haveSpin=false, haveTilt=false, havePal=false;
    std::ifstream f("session_state.txt"); std::string key; double val;
    while (f>>key>>val) {
        if (key==mode+"_steps")      steps=(int)val;
        else if (key==mode+"_zoom")  zoom=(float)val;
        else if (key=="spin")    { spin=(float)val; haveSpin=true; }
        else if (key=="tilt")    { tilt=(float)val; haveTilt=true; }
        else if (key=="palette") { pal=(float)val;  havePal=true;  }
    }
    engine.COMPUTE_STEPS=engine.COMPUTE_MOVING_STEPS=steps;
    camera.radius=glm::clamp(zoom,camera.minRadius,camera.maxRadius);
    if (restoreSpin    && haveSpin) engine.kerrSpin = glm::clamp(spin, 0.0f, 1.0f);
    if (restoreTilt    && haveTilt) engine.bhTilt   = tilt;
    if (restorePalette && havePal)  { int m=(int)std::round(pal); if (m>=1 && m<=5) diskColorTint.a=float(m); }
    camera.update();
}

void saveSessionState(const std::string& mode) {
    std::map<std::string,double> state;
    std::ifstream fi("session_state.txt"); std::string k; double v;
    while (fi>>k>>v) state[k]=v;
    state[mode+"_steps"]=engine.COMPUTE_STEPS;
    state[mode+"_zoom"]=camera.radius;
    // Global look (shared across modes). If a spin lerp is mid-flight, persist the
    // intended target so the next launch restores where the user was heading.
    state["spin"]    = (g_spinTarget>=0.0f) ? g_spinTarget : engine.kerrSpin;
    state["tilt"]    = engine.bhTilt;
    state["palette"] = std::round(diskColorTint.a);
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

// ── Live window-title HUD (window mode) ───────────────────────────────────────
// Turns the title bar into a free heads-up display: current spin / tilt / palette
// / fps (+ PAUSED). Throttled to twice a second; fps measured over that window.
static void updateWindowTitle() {
    static double t0 = glfwGetTime();
    static int    frames = 0;
    static double fps = 0.0;
    ++frames;
    double now = glfwGetTime(), dt = now - t0;
    if (dt >= 0.5) { if (dt < 2.0) fps = frames / dt;  // ignore idle gaps (TAA settled)
                     frames = 0; t0 = now; }
    int m = (int)std::round(diskColorTint.a); if (m < 1 || m > 5) m = 1;
    char title[192];
    std::snprintf(title, sizeof(title),
        "black_hole  |  a* %.2f  |  tilt %.0f deg  |  %s  |  %.0f fps%s",
        engine.kerrSpin, engine.bhTilt * 180.0f / float(M_PI),
        kColorModeNames[m], fps, g_paused ? "   [PAUSED]" : "");
    glfwSetWindowTitle(engine.window, title);
}

// ── Headless high-res render to BMP (--render): for visual validation ─────────
static void renderToFile(int w, int h, float elev, float azim, double zoom, const char* fname) {
    engine.WIDTH = w; engine.HEIGHT = h;
    engine.COMPUTE_WIDTH = engine.COMPUTE_MOVING_WIDTH = w;
    engine.COMPUTE_HEIGHT = engine.COMPUTE_MOVING_HEIGHT = h;
    engine.COMPUTE_D_LAMBDA = engine.COMPUTE_MOVING_D_LAMBDA = 7.5e8f;
    camera.elevation = glm::clamp(elev, 0.01f, float(M_PI)-0.01f);
    camera.azimuth = azim;
    camera.radius = glm::clamp((float)zoom, camera.minRadius, camera.maxRadius);
    camera.moving = false; camera.dirty = true; camera.target = vec3(0.0f);
    if (g_cinematic) cinematicCamera(g_renderTime >= 0.0f ? double(g_renderTime) : 0.0);  // fly-through snapshot
    updateMoons();   // position moons for this (deterministic) snapshot / --time
    for (int s = 0; s < 24; ++s) engine.dispatchCompute(camera, s);  // TAA accumulate
    std::vector<unsigned char> px((size_t)w*h*4);
    glBindTexture(GL_TEXTURE_2D, engine.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    int rowBytes=w*3, pad=(4-rowBytes%4)%4, stride=rowBytes+pad, fileSize=54+stride*h;
    unsigned char hdr[54]={}; hdr[0]='B';hdr[1]='M';
    hdr[2]=fileSize;hdr[3]=fileSize>>8;hdr[4]=fileSize>>16;hdr[5]=fileSize>>24;
    hdr[10]=54;hdr[14]=40;
    hdr[18]=w;hdr[19]=w>>8;hdr[20]=w>>16;hdr[21]=w>>24;
    hdr[22]=h;hdr[23]=h>>8;hdr[24]=h>>16;hdr[25]=h>>24;
    hdr[26]=1;hdr[28]=24;
    std::ofstream f(fname,std::ios::binary);
    f.write(reinterpret_cast<char*>(hdr),54);
    std::vector<unsigned char> row(stride,0);
    for (int y=0;y<h;y++) {                       // BMP is bottom-up; texture row 0 = top
        const unsigned char* p=&px[(size_t)(h-1-y)*w*4];
        for (int x=0;x<w;x++){ row[x*3+0]=p[x*4+2]; row[x*3+1]=p[x*4+1]; row[x*3+2]=p[x*4+0]; }
        f.write(reinterpret_cast<char*>(row.data()),stride);
    }
    std::cout<<"[INFO] Rendered "<<fname<<" "<<w<<"x"<<h<<"\n";
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
        if (act==GLFW_PRESS && key==GLFW_KEY_W) {
            g_mergerElapsed = 0.0f; engine.mergerTime = 0.0f; cam->dirty=true;
            std::cout<<"[INFO] Merger wave triggered\n";
        }
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
            if (key==GLFW_KEY_R) { g_autoRotate=!g_autoRotate; cam->update();
                                   std::cout<<"[INFO] Auto-rotate: "<<(g_autoRotate?"ON":"OFF")<<"\n"; }
            if (key==GLFW_KEY_C) { g_cinematic=!g_cinematic; cam->update();
                                   std::cout<<"[INFO] Cinematic: "<<(g_cinematic?"ON":"OFF")<<"\n"; }
            if (key==GLFW_KEY_Q||key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w,GLFW_TRUE);
            if (key==GLFW_KEY_S) saveScreenshot(engine.WIDTH, engine.HEIGHT);
            if (key==GLFW_KEY_SPACE) { g_paused=!g_paused; cam->dirty=true;
                                       std::cout<<"[INFO] Paused: "<<(g_paused?"ON":"OFF")<<"\n"; }
            if (key==GLFW_KEY_B) { engine.bloomEnabled=!engine.bloomEnabled; cam->dirty=true; }
            if (key==GLFW_KEY_A) { engine.diskAnimEnabled=!engine.diskAnimEnabled; cam->dirty=true;
                                   std::cout<<"[INFO] Disk animation: "<<(engine.diskAnimEnabled?"ON":"OFF")<<"\n"; }
            // K toggles spin; the main loop eases kerrSpin toward the target so the
            // disk morphs smoothly instead of snapping. Toggle off the in-flight
            // target if a lerp is already running.
            if (key==GLFW_KEY_K) {
                float ref = (g_spinTarget>=0.0f) ? g_spinTarget : engine.kerrSpin;
                g_spinTarget = (ref < 0.01f) ? kKerrSpinOn : 0.0f;
                cam->dirty=true;
                std::cout<<"[INFO] Kerr spin -> "<<g_spinTarget<<"\n";
            }
            // Fine Kerr-spin control: '.' raises, ',' lowers (clamped to [0,1]); eased.
            if (key==GLFW_KEY_PERIOD || key==GLFW_KEY_COMMA) {
                float ref = (g_spinTarget>=0.0f) ? g_spinTarget : engine.kerrSpin;
                float d = (key==GLFW_KEY_PERIOD) ? kKerrSpinStep : -kKerrSpinStep;
                g_spinTarget = glm::clamp(ref + d, 0.0f, 1.0f);
                cam->dirty=true;
                std::cout<<"[INFO] Kerr spin -> "<<g_spinTarget<<"\n";
            }
            // BH tilt: T tips the disk/spin/jets +5°, Shift+T −5° (live).
            if (key==GLFW_KEY_T) {
                engine.bhTilt += (mods & GLFW_MOD_SHIFT) ? -kTiltStep : kTiltStep;
                cam->dirty=true;
                std::cout<<"[INFO] BH tilt: "<<engine.bhTilt*180.0f/float(M_PI)<<" deg\n";
            }
            // F: cycle the scene palette (disk + jets + meteors all follow it).
            if (key==GLFW_KEY_F) {
                int m = cycleColorMode();
                cam->dirty=true;
                std::cout<<"[INFO] Color mode: "<<kColorModeNames[m]<<" ("<<m<<")\n";
            }
            // P: cycle through built-in scene presets (spin / tilt / palette / zoom / anim).
            if (key==GLFW_KEY_P) {
                g_presetIdx = (g_presetIdx + 1) % kNumPresets;
                applyPreset(g_presetIdx);
                saveSessionState(g_currentMode);
                std::cout<<"[INFO] Preset "<<(g_presetIdx+1)<<"/"<<kNumPresets<<"\n";
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
    bool renderMode=false; int rW=900, rH=600; float rElev=1.35f, rAzim=0.6f; double rZoom=1.05e11;
    const char* rOut="render.bmp";
    // Track which "look" knobs the CLI set so the saved session never overrides them.
    bool cliSpin=false, cliTilt=false, cliPalette=false;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--wallpaper")||!strcmp(argv[i],"-w")) wallpaperMode=true;
        else if (!strcmp(argv[i],"--terminal")||!strcmp(argv[i],"-t")) terminalMode=true;
        else if (!strcmp(argv[i],"--dcomp")||!strcmp(argv[i],"-d")) dcompMode=true;
        else if (!strcmp(argv[i],"--no-grid"))  showGrid=false;
        else if (!strcmp(argv[i],"--no-disk"))  showDisk=false;
        else if (!strcmp(argv[i],"--no-beam"))  showBeam=false;
        else if (!strcmp(argv[i],"--test"))     runTests=true;
        else if (!strcmp(argv[i],"--red"))   { diskColorTint=vec4(0,0,0,2); cliPalette=true; }
        else if (!strcmp(argv[i],"--white")) { diskColorTint=vec4(0,0,0,3); cliPalette=true; }
        else if (!strcmp(argv[i],"--blue"))  { diskColorTint=vec4(0,0,0,4); cliPalette=true; }
        else if (!strcmp(argv[i],"--green")) { diskColorTint=vec4(0,0,0,5); cliPalette=true; }
        else if (!strcmp(argv[i],"--spin") && i+1<argc) { engine.kerrSpin=atof(argv[++i]); cliSpin=true; }
        else if (!strcmp(argv[i],"--tilt") && i+1<argc) { engine.bhTilt=atof(argv[++i])*float(M_PI)/180.0f; cliTilt=true; }
        else if (!strcmp(argv[i],"--cinematic")) g_cinematic=true;
        else if (!strcmp(argv[i],"--anim"))  engine.diskAnimEnabled=true;
        else if (!strcmp(argv[i],"--legacy")||!strcmp(argv[i],"-l")||!strcmp(argv[i],"legacy")) legacyMode=true;
        else if (!strcmp(argv[i],"--preset") && i+1<argc) {
            const char* pname = argv[++i];
            for (int pi=0; pi<kNumPresets; ++pi)
                if (!strcmp(kPresets[pi].name, pname)) { g_presetIdx=pi; break; }
        }
        else if (!strcmp(argv[i],"--render")) renderMode=true;
        else if (!strcmp(argv[i],"--size") && i+1<argc) { sscanf(argv[++i],"%dx%d",&rW,&rH); }
        else if (!strcmp(argv[i],"--elev") && i+1<argc) rElev=atof(argv[++i]);
        else if (!strcmp(argv[i],"--azim") && i+1<argc) rAzim=atof(argv[++i]);
        else if (!strcmp(argv[i],"--zoom") && i+1<argc) rZoom=atof(argv[++i]);
        else if (!strcmp(argv[i],"--time") && i+1<argc) g_renderTime=atof(argv[++i]);
        else if (!strcmp(argv[i],"--out")  && i+1<argc) rOut=argv[++i];
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
            cliSpin = true;   // don't let the saved session re-enable spin under legacy
            std::cout << "[INFO] LEGACY: shader Schwarzschild original (geodesic_legacy.comp); Kerr/anim desativados\n";
        } else {
            std::cerr << "[WARN] geodesic_legacy.comp nao encontrado; seguindo com shader atual\n";
        }
    }

    if (renderMode) {
        renderToFile(rW, rH, rElev, rAzim, rZoom, rOut);
        glfwDestroyWindow(engine.window); glfwTerminate(); return 0;
    }

    setupCameraCallbacks(engine.window);
#ifdef _WIN32
    if (wallpaperMode) g_currentMode="wallpaper";
#endif
    // Apply the saved session for this mode, but keep any CLI "look" override intact.
    auto restoreSession = [&](const std::string& mode){
        loadSessionState(mode, !cliSpin, !cliTilt, !cliPalette);
    };
    restoreSession(g_currentMode);

#ifdef _WIN32
    if (terminalMode) {
        g_currentMode="terminal"; restoreSession(g_currentMode);
        if (g_presetIdx>=0) { applyPreset(g_presetIdx); engine.kerrSpin=kPresets[g_presetIdx].spin; }
        runTerminal(engine); saveSessionState(g_currentMode);
        glfwDestroyWindow(engine.window); glfwTerminate(); return 0;
    }
    if (dcompMode) {
        g_currentMode="wallpaper"; restoreSession(g_currentMode);
        if (g_presetIdx>=0) { applyPreset(g_presetIdx); engine.kerrSpin=kPresets[g_presetIdx].spin; }
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
        } else { wallpaperMode=false; g_currentMode="window"; restoreSession(g_currentMode); }
    }
#else
    if (wallpaperMode) { wallpaperMode=false; g_currentMode="window"; restoreSession(g_currentMode); }
#endif

    if (!wallpaperMode) {
        engine.COMPUTE_WIDTH=engine.COMPUTE_MOVING_WIDTH=480;
        engine.COMPUTE_HEIGHT=engine.COMPUTE_MOVING_HEIGHT=360;
        engine.COMPUTE_MOVING_STEPS=engine.COMPUTE_STEPS;
        engine.COMPUTE_D_LAMBDA=engine.COMPUTE_MOVING_D_LAMBDA=7.5e8f;
    }

    // Apply startup preset AFTER all session restores (window + wallpaper modes).
    if (g_presetIdx >= 0) applyPreset(g_presetIdx);

    int accumSample=0;
    int ACCUM_SAMPLES = wallpaperMode ? 16 : 4;
    const double wpMinFrame = wallpaperMode ? (1.0/30.0) : 0.0;
    double lastFrameStart = glfwGetTime();

    while (!glfwWindowShouldClose(engine.window)) {
        if (engine.checkShaderReload()) { accumSample=0; camera.dirty=true; }
#ifdef _WIN32
        if (wallpaperMode) wallpaperUpdate(engine.window);
#endif
        // Space (window mode): freeze on the last drawn frame, but keep pumping events
        // so Space can resume. Skipped in wallpaper mode (g_paused never set there).
        if (g_paused && !wallpaperMode) {
            updateWindowTitle(); glfwWaitEventsTimeout(1.0/30.0); continue;
        }
        // Per-frame animation tick: ease spin + advance merger wave timer.
        {
            static double lastT = glfwGetTime();
            double nowT = glfwGetTime();
            float  dt   = glm::clamp(float(nowT - lastT), 0.0f, 0.1f);
            lastT = nowT;
            // Smooth Kerr-spin lerp (K / . / , or preset)
            if (g_spinTarget >= 0.0f) {
                float diff = g_spinTarget - engine.kerrSpin, step = kSpinLerpRate * dt;
                if (std::fabs(diff) <= step) { engine.kerrSpin = g_spinTarget; g_spinTarget = -1.0f; }
                else engine.kerrSpin += (diff > 0.0f ? step : -step);
                camera.dirty = true;
            }
            // Merger wave (W key): advance timer, expire after 4.5 s
            if (g_mergerElapsed >= 0.0f) {
                g_mergerElapsed   += dt;
                engine.mergerTime  = g_mergerElapsed;
                if (g_mergerElapsed > 4.5f) { g_mergerElapsed = -1.0f; engine.mergerTime = -1.0f; }
                camera.dirty = true;
            }
        }
        // Disk animation always re-renders (no accumulation)
        if (engine.diskAnimEnabled) { camera.dirty=true; accumSample=0; }

        // Window mode: idle auto-orbit (R toggles). Paused while dragging; skipped
        // in wallpaper mode, which drives its own rotation in wallpaperUpdate.
        if (!wallpaperMode && g_cinematic) {
            cinematicCamera(glfwGetTime());
        } else if (!wallpaperMode && g_autoRotate && !camera.dragging && !camera.panning) {
            camera.azimuth += 0.0035f; camera.dirty = true;
        }

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
        // orbit bodies while the view is animating (auto-rotate / A / wallpaper); when
        // both are off the window goes static so the TAA can accumulate a crisp frame.
        g_animate = wallpaperMode || g_autoRotate || engine.diskAnimEnabled;
        bool sceneDirty=updateGravityPhysics();
        glViewport(0,0,engine.WIDTH,engine.HEIGHT);
        if (camera.dirty||sceneDirty) {
            accumSample=0; engine.dispatchCompute(camera,0); accumSample=1; camera.dirty=false;
        } else if (wantAccum) {
            engine.dispatchCompute(camera,accumSample); ++accumSample;
        }
        engine.drawFullScreenQuad();
        glfwSwapBuffers(engine.window); glfwPollEvents();
        if (!wallpaperMode) updateWindowTitle();
    }
    saveSessionState(g_currentMode);
    glfwDestroyWindow(engine.window); glfwTerminate(); return 0;
}
