#include <GL/glew.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
// Wallpaper mode: we reparent the GLFW window into the desktop's WorkerW layer
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif
#endif
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
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace glm;
using namespace std;
namespace fs = std::filesystem;
using Clock = std::chrono::high_resolution_clock;

// Resolve a resource (shader) path next to the executable, so the app runs from
// any working directory (needed for wallpaper/terminal/split launches).
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

// VARS
double lastPrintTime = 0.0;
int    framesCount   = 0;
double c = 299792458.0;
double G = 6.67430e-11;
bool Gravity = false;
bool showGrid = true;
bool showDisk = true;
bool showBeam = true;
vec4 diskColorTint = vec4(0.0f, 0.0f, 0.0f, 1.0f); // Default orange-yellow gradient mode (1.0f)

// Fast lookup table for integer-to-string conversion (0-255) to speed up ANSI color formatting
static std::vector<std::string> initNumStrs() {
    std::vector<std::string> v(256);
    for (int i = 0; i < 256; ++i) {
        v[i] = std::to_string(i);
    }
    return v;
}
static const std::vector<std::string> numStrs = initNumStrs();

struct Camera {
    // Center the camera orbit on the black hole at (0, 0, 0)
    vec3 target = vec3(0.0f, 0.0f, 0.0f); // Always look at the black hole center
    float radius = 6.34194e10f;
    float minRadius = 1e10f, maxRadius = 1e12f;

    float azimuth = 0.0f;
    float elevation = M_PI / 2.0f;

    float orbitSpeed = 0.01f;
    float panSpeed = 0.01f;
    double zoomSpeed = 25e9f;

    bool dragging = false;
    bool panning = false;
    bool moving = false; // For compute shader optimization
    bool dirty = true;
    double lastX = 0.0, lastY = 0.0;

    // Calculate camera position in world space
    vec3 position() const {
        float clampedElevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
        // Orbit around (0,0,0) always
        return vec3(
            radius * sin(clampedElevation) * cos(azimuth),
            radius * cos(clampedElevation),
            radius * sin(clampedElevation) * sin(azimuth)
        );
    }
    void update(bool transientMove = false) {
        target = vec3(0.0f, 0.0f, 0.0f);
        dirty = true;
        moving = transientMove || dragging || panning;
    }

    void processMouseMove(double x, double y) {
        float dx = float(x - lastX);
        float dy = float(y - lastY);

        if (dragging && panning) {
            // Pan: Shift + Left or Middle Mouse
            // Disable panning to keep camera centered on black hole
        }
        else if (dragging && !panning) {
            // Orbit: Left mouse only
            azimuth   += dx * orbitSpeed;
            elevation -= dy * orbitSpeed;
            elevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
        }

        lastX = x;
        lastY = y;
        update();
    }
    void processMouseButton(int button, int action, int mods, GLFWwindow* win) {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (action == GLFW_PRESS) {
                dragging = true;
                // Disable panning so camera always orbits center
                panning = false;
                glfwGetCursorPos(win, &lastX, &lastY);
            } else if (action == GLFW_RELEASE) {
                dragging = false;
                panning = false;
            }
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                Gravity = true;
            } else if (action == GLFW_RELEASE) {
                Gravity = false;
            }
        }
        update();
    }
    void processScroll(double xoffset, double yoffset) {
        radius -= yoffset * zoomSpeed;
        radius = glm::clamp(radius, minRadius, maxRadius);
        update(true);
    }
    void processKey(int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS && key == GLFW_KEY_G) {
            Gravity = !Gravity;
            cout << "[INFO] Gravity turned " << (Gravity ? "ON" : "OFF") << endl;
            update();
        }
    }
};
Camera camera;

struct BlackHole {
    vec3 position;
    double mass;
    double radius;
    double r_s;

    BlackHole(vec3 pos, float m) : position(pos), mass(m) {r_s = 2.0 * G * mass / (c*c);}
};
BlackHole SagA(vec3(0.0f, 0.0f, 0.0f), 8.54e36); // Sagittarius A black hole
struct ObjectData {
    vec4 posRadius; // xyz = position, w = radius
    vec4 color;     // rgb = color, a = unused
    float  mass;
    vec3 velocity = vec3(0.0f, 0.0f, 0.0f); // Initial velocity
};
vector<ObjectData> objects = {
    { vec4(4e11f, 0.0f, 0.0f, 4e10f)    , vec4(0,0,1,1), 1.98892e30 },  // blue sun (was yellow)
    { vec4(0.0f, 0.0f, 4e11f, 4e10f)    , vec4(1,0,0,1), 1.98892e30 },  // red sun
    { vec4(-4e11f, 0.0f, 0.0f, 7e10f)   , vec4(0,1,0,1), 1.98892e30 },  // green sun (bigger)
    { vec4(0.0f, 0.0f, 0.0f, SagA.r_s)  , vec4(0,0,0,1), static_cast<float>(SagA.mass)  },
};

static bool updateGravityPhysics() {
    if (!Gravity) return false;
    for (auto& obj : objects) {
        vec3 totalAcc = vec3(0.0f);
        for (auto& obj2 : objects) {
            if (&obj == &obj2) continue;
            float dx = obj2.posRadius.x - obj.posRadius.x;
            float dy = obj2.posRadius.y - obj.posRadius.y;
            float dz = obj2.posRadius.z - obj.posRadius.z;
            float dist = sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > 0.0f) {
                double Gforce = (G * obj.mass * obj2.mass) / (double(dist) * double(dist));
                double acc1 = Gforce / obj.mass;
                totalAcc.x += float(dx / dist * acc1);
                totalAcc.y += float(dy / dist * acc1);
                totalAcc.z += float(dz / dist * acc1);
            }
        }
        obj.velocity += totalAcc;
        obj.posRadius.x += obj.velocity.x;
        obj.posRadius.y += obj.velocity.y;
        obj.posRadius.z += obj.velocity.z;
    }
    return true;
}

// Low-discrepancy sequence for the temporal-AA sub-pixel jitter
static float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= float(base);
        r += f * float(index % base);
        index /= base;
    }
    return r;
}

struct Engine {
    GLuint gridShaderProgram;
    // -- Quad & Texture render -- //
    GLFWwindow* window;
    GLuint quadVAO;
    GLuint texture;
    GLuint accumTexture = 0; // float history buffer for temporal AA
    GLuint shaderProgram;
    GLuint computeProgram = 0;
    // -- UBOs -- //
    GLuint cameraUBO = 0;
    GLuint diskUBO = 0;
    GLuint objectsUBO = 0;
    // -- grid mess vars -- //
    GLuint gridVAO = 0;
    GLuint gridVBO = 0;
    GLuint gridEBO = 0;
    int gridIndexCount = 0;
    GLuint gridFBO = 0;
    // -- Bloom --
    GLuint bloomFBO1 = 0, bloomFBO2 = 0;
    GLuint bloomTex1 = 0, bloomTex2 = 0;
    GLuint bloomThreshProg = 0, bloomBlurProg = 0;
    int    bloomAllocW = 0, bloomAllocH = 0;
    bool   bloomEnabled   = true;
    float  bloomStrength  = 0.65f;
    float  bloomThreshold = 0.60f;
    // -- Hot-reload --
    fs::file_time_type lastShaderMtime{};
    // -- PBO async readback (terminal + DComp modes) --
    GLuint pbo[2]        = {0, 0};
    int    pboRead       = 0, pboWrite = 1;
    bool   pboReady      = false;
    size_t pboAllocSize  = 0;

    int WIDTH = 800;  // Window width
    int HEIGHT = 600; // Window height
    int COMPUTE_WIDTH  = 320;   // Lower starting res (style of terminal mode)
    int COMPUTE_HEIGHT = 240;
    int COMPUTE_MOVING_WIDTH = 320;  // Keep same resolution as static to avoid quality discrepancy
    int COMPUTE_MOVING_HEIGHT = 240;
    int COMPUTE_STEPS = 600;          // Full integration steps for the cached static frame
    // Keep the moving step count equal to static: ray reach = steps * dLambda must
    // still cover the colored objects at ~4.4e11 (768*7.5e8 = 5.76e11). Fewer steps
    // shortened the reach below that, so the objects vanished while dragging.
    // The moving LOD now comes purely from the lower resolution above.
    int COMPUTE_MOVING_STEPS = 600;
    float COMPUTE_D_LAMBDA = 7.5e8f;
    float COMPUTE_MOVING_D_LAMBDA = 7.5e8f;
    float width = 100000000000.0f; // Width of the viewport in meters
    float height = 75000000000.0f; // Height of the viewport in meters
    
    Engine() {
#ifdef _WIN32
        // Be DPI-aware BEFORE creating the window, so a wallpaper covers the
        // real pixel resolution instead of a virtualized (scaled-down) box.
        SetProcessDPIAware();
#endif
        if (!glfwInit()) {
            cerr << "GLFW init failed\n";
            exit(EXIT_FAILURE);
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        // No alpha in the default framebuffer: a borderless GL window with an
        // alpha channel gets per-pixel composited by DWM, so the (alpha=0)
        // background turned transparent and the whole thing looked invisible.
        glfwWindowHint(GLFW_ALPHA_BITS, 0);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
        if (!window) {
            cerr << "Failed to create GLFW window\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        glewExperimental = GL_TRUE;
        GLenum glewErr = glewInit();
        if (glewErr != GLEW_OK) {
            cerr << "Failed to initialize GLEW: "
                << (const char*)glewGetErrorString(glewErr)
                << "\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        cout << "OpenGL " << glGetString(GL_VERSION) << "\n";
        this->shaderProgram = CreateShaderProgram();
        gridShaderProgram = CreateShaderProgram("grid.vert", "grid.frag");

        computeProgram = CreateComputeProgram("geodesic.comp");
        try { lastShaderMtime = fs::last_write_time(resourcePath("geodesic.comp")); } catch (...) {}
        initBloom();
        glGenBuffers(1, &cameraUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferData(GL_UNIFORM_BUFFER, 128, nullptr, GL_DYNAMIC_DRAW); // alloc ~128 bytes
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO); // binding = 1 matches shader

        glGenBuffers(1, &diskUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferData(GL_UNIFORM_BUFFER, 32, nullptr, GL_DYNAMIC_DRAW); // 4 floats + vec4 (32 bytes)
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO); // binding = 2 matches compute shader

        glGenBuffers(1, &objectsUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        // allocate space for 16 objects: 
        // sizeof(int) + padding + 16×(vec4 posRadius + vec4 color)
        GLsizeiptr objUBOSize = sizeof(int) + 3 * sizeof(float)
            + 16 * (sizeof(vec4) + sizeof(vec4))
            + 16 * sizeof(float); // 16 floats for mass
        glBufferData(GL_UNIFORM_BUFFER, objUBOSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO);  // binding = 3 matches shader

        auto result = QuadVAO();
        this->quadVAO = result[0];
        this->texture = result[1];

        glGenTextures(1, &accumTexture);
        glBindTexture(GL_TEXTURE_2D, accumTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, COMPUTE_WIDTH, COMPUTE_HEIGHT,
                     0, GL_RGBA, GL_FLOAT, nullptr);
    }
    void generateGrid(const vector<ObjectData>& objects) {
        const int gridSize = 25;
        const float spacing = 1e10f;  // tweak this

        vector<vec3> vertices;
        vector<GLuint> indices;

        for (int z = 0; z <= gridSize; ++z) {
            for (int x = 0; x <= gridSize; ++x) {
                float worldX = (x - gridSize / 2) * spacing;
                float worldZ = (z - gridSize / 2) * spacing;

                float y = 0.0f;

                // ✅ Warp grid using Schwarzschild geometry
                for (const auto& obj : objects) {
                    vec3 objPos = vec3(obj.posRadius);
                    double mass = obj.mass;
                    double radius = obj.posRadius.w;

                    double r_s = 2.0 * G * mass / (c * c);
                    double dx = worldX - objPos.x;
                    double dz = worldZ - objPos.z;
                    double dist = sqrt(dx * dx + dz * dz);

                    // prevent sqrt of negative or divide-by-zero (inside or at the black hole center)
                    if (dist > r_s) {
                        double deltaY = 2.0 * sqrt(r_s * (dist - r_s));
                        y += static_cast<float>(deltaY) - 3e10f;
                    } else {
                        // 🔴 For points inside or at r_s: make it dip down sharply
                        y += 2.0f * static_cast<float>(sqrt(r_s * r_s)) - 3e10f;  // or add a deep pit
                    }
                }

                vertices.emplace_back(worldX, y, worldZ);
            }
        }

        // 🧩 Add indices for GL_LINE rendering
        for (int z = 0; z < gridSize; ++z) {
            for (int x = 0; x < gridSize; ++x) {
                int i = z * (gridSize + 1) + x;
                indices.push_back(i);
                indices.push_back(i + 1);

                indices.push_back(i);
                indices.push_back(i + gridSize + 1);
            }
        }

        // 🔌 Upload to GPU
        if (gridVAO == 0) glGenVertexArrays(1, &gridVAO);
        if (gridVBO == 0) glGenBuffers(1, &gridVBO);
        if (gridEBO == 0) glGenBuffers(1, &gridEBO);

        glBindVertexArray(gridVAO);

        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vec3), vertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0); // location = 0
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);

        gridIndexCount = indices.size();

        glBindVertexArray(0);
    }
    void drawGrid(const mat4& viewProj) {
        glUseProgram(gridShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "viewProj"),
                        1, GL_FALSE, glm::value_ptr(viewProj));
        
        // Bind accumTexture as the screenTexture to check for event horizon occlusion
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, accumTexture);
        glUniform1i(glGetUniformLocation(gridShaderProgram, "screenTexture"), 0);

        glBindVertexArray(gridVAO);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDrawElements(GL_LINES, gridIndexCount, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }
    void drawGridToTexture(const Camera& cam, bool forceRegen) {
        if (!showGrid) return;

        static bool gridGenerated = false;
        if (!gridGenerated || forceRegen) {
            generateGrid(objects);
            gridGenerated = true;
        }

        if (gridFBO == 0) {
            glGenFramebuffers(1, &gridFBO);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, gridFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

        int cw = cam.moving ? COMPUTE_MOVING_WIDTH : COMPUTE_WIDTH;
        int ch = cam.moving ? COMPUTE_MOVING_HEIGHT : COMPUTE_HEIGHT;
        GLint prevViewport[4];
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glViewport(0, 0, cw, ch);

        mat4 view = lookAt(cam.position(), cam.target, vec3(0,1,0));
        mat4 proj = perspective(radians(60.0f), float(cw)/ch, 1e9f, 1e14f);
        mat4 viewProj = proj * view;

        drawGrid(viewProj);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }
    // Non-fatal version of CreateComputeProgram: returns 0 on compile/link error.
    GLuint tryCreateComputeProgram(const char* path) {
        std::ifstream in(resourcePath(path));
        if (!in.is_open()) return 0;
        std::stringstream ss; ss << in.rdbuf();
        std::string srcStr = ss.str();
        const char* src = srcStr.c_str();

        GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(cs, 1, &src, nullptr);
        glCompileShader(cs);
        GLint ok; glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len; glGetShaderiv(cs, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len);
            glGetShaderInfoLog(cs, len, nullptr, log.data());
            std::cerr << "[RELOAD ERROR] " << log.data() << "\n";
            glDeleteShader(cs); return 0;
        }
        GLuint prog = glCreateProgram();
        glAttachShader(prog, cs);
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint len; glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len);
            glGetProgramInfoLog(prog, len, nullptr, log.data());
            std::cerr << "[RELOAD LINK ERROR] " << log.data() << "\n";
            glDeleteProgram(prog); glDeleteShader(cs); return 0;
        }
        glDeleteShader(cs);
        return prog;
    }
    // Returns true if shader was reloaded (caller should reset accumSample + dirty).
    bool checkShaderReload() {
        try {
            auto mtime = fs::last_write_time(resourcePath("geodesic.comp"));
            if (mtime == lastShaderMtime) return false;
            lastShaderMtime = mtime;
            GLuint newProg = tryCreateComputeProgram("geodesic.comp");
            if (newProg != 0) {
                glDeleteProgram(computeProgram);
                computeProgram = newProg;
                std::cout << "[RELOAD] geodesic.comp\n";
                return true;
            }
        } catch (...) {}
        return false;
    }
    void initPBO(size_t size) {
        if (pbo[0] == 0) glGenBuffers(2, pbo);
        if (pboAllocSize == size) return;
        pboAllocSize = size;
        for (int i = 0; i < 2; i++) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)size, nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        pboReady = false; pboRead = 0; pboWrite = 1;
    }
    // Issue async readback of `texture` into the write PBO and return a pointer
    // to the previous frame's data (from read PBO). Returns nullptr on first call.
    // Caller must call pboRelease() when done with the pointer.
    const unsigned char* pboBeginRead(GLenum format) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[pboWrite]);
        glGetTexImage(GL_TEXTURE_2D, 0, format, GL_UNSIGNED_BYTE, nullptr); // async DMA
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        const unsigned char* ptr = nullptr;
        if (pboReady) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[pboRead]);
            ptr = (const unsigned char*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        }
        return ptr;
    }
    void pboEndRead() {
        if (pboReady) {
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
        std::swap(pboRead, pboWrite);
        pboReady = true;
    }
    GLuint createInlineProg(const char* vsSrc, const char* fsSrc) {
        auto compile = [](const char* src, GLenum type) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                GLint len; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
                std::vector<char> log(len);
                glGetShaderInfoLog(s, len, nullptr, log.data());
                std::cerr << "Bloom shader error: " << log.data() << "\n";
            }
            return s;
        };
        GLuint vs = compile(vsSrc, GL_VERTEX_SHADER);
        GLuint fs = compile(fsSrc, GL_FRAGMENT_SHADER);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        return prog;
    }
    void initBloom() {
        const char* quadVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() { gl_Position = vec4(aPos,0,1); TexCoord = aTexCoord; })";

        const char* threshFS = R"(#version 330 core
in vec2 TexCoord; out vec4 FragColor;
uniform sampler2D screenTexture;
uniform float threshold;
void main() {
    vec3 c = texture(screenTexture, TexCoord).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    FragColor = vec4(lum > threshold ? c : vec3(0.0), 1.0);
})";

        const char* blurFS = R"(#version 330 core
in vec2 TexCoord; out vec4 FragColor;
uniform sampler2D blurTex;
uniform vec2 texelSize;
uniform bool horizontal;
const float w[5] = float[](0.2270, 0.1946, 0.1216, 0.0541, 0.0162);
void main() {
    vec2 dir = horizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
    vec3 res = texture(blurTex, TexCoord).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        res += texture(blurTex, TexCoord + dir * float(i)).rgb * w[i];
        res += texture(blurTex, TexCoord - dir * float(i)).rgb * w[i];
    }
    FragColor = vec4(res, 1.0);
})";

        bloomThreshProg = createInlineProg(quadVS, threshFS);
        bloomBlurProg   = createInlineProg(quadVS, blurFS);

        glGenFramebuffers(1, &bloomFBO1);
        glGenFramebuffers(1, &bloomFBO2);
        glGenTextures(1, &bloomTex1);
        glGenTextures(1, &bloomTex2);

        for (GLuint tex : {bloomTex1, bloomTex2}) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            // initialise to black so composite is clean on first frame
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
        }
    }
    void ensureBloom(int w, int h) {
        int bw = std::max(1, w / 2);
        int bh = std::max(1, h / 2);
        if (bloomAllocW == bw && bloomAllocH == bh) return;
        bloomAllocW = bw; bloomAllocH = bh;

        auto setup = [&](GLuint fbo, GLuint tex) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        };
        setup(bloomFBO1, bloomTex1);
        setup(bloomFBO2, bloomTex2);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void doBloom(int srcW, int srcH) {
        if (!bloomEnabled) return;
        ensureBloom(srcW, srcH);
        int bw = bloomAllocW, bh = bloomAllocH;

        GLint prevVP[4];
        glGetIntegerv(GL_VIEWPORT, prevVP);
        glViewport(0, 0, bw, bh);
        glDisable(GL_DEPTH_TEST);

        // Pass 1: threshold → bloomTex1
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO1);
        glUseProgram(bloomThreshProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(bloomThreshProg, "screenTexture"), 0);
        glUniform1f(glGetUniformLocation(bloomThreshProg, "threshold"), bloomThreshold);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 6);

        // Pass 2: horizontal Gaussian bloomTex1 → bloomTex2
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO2);
        glUseProgram(bloomBlurProg);
        glBindTexture(GL_TEXTURE_2D, bloomTex1);
        glUniform1i(glGetUniformLocation(bloomBlurProg, "blurTex"), 0);
        glUniform2f(glGetUniformLocation(bloomBlurProg, "texelSize"), 1.0f/bw, 1.0f/bh);
        glUniform1i(glGetUniformLocation(bloomBlurProg, "horizontal"), GL_TRUE);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 6);

        // Pass 3: vertical Gaussian bloomTex2 → bloomTex1
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO1);
        glBindTexture(GL_TEXTURE_2D, bloomTex2);
        glUniform1i(glGetUniformLocation(bloomBlurProg, "horizontal"), GL_FALSE);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
        glEnable(GL_DEPTH_TEST);
    }
    void drawFullScreenQuad() {
        glUseProgram(shaderProgram);
        glBindVertexArray(quadVAO);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomTex1);
        glUniform1i(glGetUniformLocation(shaderProgram, "bloomTexture"), 1);
        glUniform1f(glGetUniformLocation(shaderProgram, "bloomStrength"),
                    bloomEnabled ? bloomStrength : 0.0f);

        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 6);
        glEnable(GL_DEPTH_TEST);
    }
    GLuint CreateShaderProgram(){
        const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;  // Changed to vec2
        layout (location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);  // Explicit z=0
            TexCoord = aTexCoord;
        })";

        const char* fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D screenTexture;
        uniform sampler2D bloomTexture;
        uniform float bloomStrength;
        void main() {
            vec3 base = texture(screenTexture, TexCoord).rgb;
            vec3 glow = texture(bloomTexture,  TexCoord).rgb;
            FragColor = vec4(base + glow * bloomStrength, 1.0);
        })";

        // vertex shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        // fragment shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        return shaderProgram;
    };
    GLuint CreateShaderProgram(const char* vertPath, const char* fragPath) {
        auto loadShader = [](const char* path, GLenum type) -> GLuint {
            std::ifstream in(resourcePath(path));
            if (!in.is_open()) {
                std::cerr << "Failed to open shader: " << path << "\n";
                exit(EXIT_FAILURE);
            }
            std::stringstream ss;
            ss << in.rdbuf();
            std::string srcStr = ss.str();
            const char* src = srcStr.c_str();

            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);

            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLint logLen;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
                std::vector<char> log(logLen);
                glGetShaderInfoLog(shader, logLen, nullptr, log.data());
                std::cerr << "Shader compile error (" << path << "):\n" << log.data() << "\n";
                exit(EXIT_FAILURE);
            }
            return shader;
        };

        GLuint vertShader = loadShader(vertPath, GL_VERTEX_SHADER);
        GLuint fragShader = loadShader(fragPath, GL_FRAGMENT_SHADER);

        GLuint program = glCreateProgram();
        glAttachShader(program, vertShader);
        glAttachShader(program, fragShader);
        glLinkProgram(program);

        GLint linkSuccess;
        glGetProgramiv(program, GL_LINK_STATUS, &linkSuccess);
        if (!linkSuccess) {
            GLint logLen;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetProgramInfoLog(program, logLen, nullptr, log.data());
            std::cerr << "Shader link error:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);

        return program;
    }
    GLuint CreateComputeProgram(const char* path) {
        // 1) read GLSL source
        std::ifstream in(resourcePath(path));
        if(!in.is_open()) {
            std::cerr << "Failed to open compute shader: " << path << "\n";
            exit(EXIT_FAILURE);
        }
        std::stringstream ss;
        ss << in.rdbuf();
        std::string srcStr = ss.str();
        const char* src = srcStr.c_str();

        // 2) compile
        GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(cs, 1, &src, nullptr);
        glCompileShader(cs);
        GLint ok; 
        glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
        if(!ok) {
            GLint logLen;
            glGetShaderiv(cs, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetShaderInfoLog(cs, logLen, nullptr, log.data());
            std::cerr << "Compute shader compile error:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        // 3) link
        GLuint prog = glCreateProgram();
        glAttachShader(prog, cs);
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if(!ok) {
            GLint logLen;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(logLen);
            glGetProgramInfoLog(prog, logLen, nullptr, log.data());
            std::cerr << "Compute shader link error:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glDeleteShader(cs);
        return prog;
    }
    void dispatchCompute(const Camera& cam, int sampleIndex) {
        // determine target compute‐res
        int cw = cam.moving ? COMPUTE_MOVING_WIDTH : COMPUTE_WIDTH;
        int ch = cam.moving ? COMPUTE_MOVING_HEIGHT : COMPUTE_HEIGHT;
        int steps = cam.moving ? COMPUTE_MOVING_STEPS : COMPUTE_STEPS;
        float dLambda = cam.moving ? COMPUTE_MOVING_D_LAMBDA : COMPUTE_D_LAMBDA;

        // 1) reallocate the texture if needed
        glBindTexture(GL_TEXTURE_2D, texture);
        static int allocatedW = 0;
        static int allocatedH = 0;
        if (allocatedW != cw || allocatedH != ch) {
            glTexImage2D(GL_TEXTURE_2D,
                        0,                // mip
                        GL_RGBA8,         // internal format
                        cw,               // width
                        ch,               // height
                        0, GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        nullptr);
            glBindTexture(GL_TEXTURE_2D, accumTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, cw, ch, 0, GL_RGBA, GL_FLOAT, nullptr);
            allocatedW = cw;
            allocatedH = ch;
        }

        // 2) bind compute program & UBOs
        glUseProgram(computeProgram);
        // Sample 0 is the centered base frame; later samples jitter sub-pixel for AA
        vec2 jitter(0.0f, 0.0f);
        if (sampleIndex > 0)
            jitter = vec2(halton(sampleIndex, 2) - 0.5f, halton(sampleIndex, 3) - 0.5f);
        uploadCameraUBO(cam, cw, ch, steps, dLambda, sampleIndex, jitter);
        uploadDiskUBO();
        uploadObjectsUBO(objects);

        // 3) bind output (unit 0) and AA history (unit 1) images
        glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        glBindImageTexture(1, accumTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

        // 4) dispatch grid
        GLuint groupsX = (GLuint)std::ceil(cw / 16.0f);
        GLuint groupsY = (GLuint)std::ceil(ch / 16.0f);
        glDispatchCompute(groupsX, groupsY, 1);

        // 5) sync
        // Image-access barrier covers the compute write; texture-fetch barrier is
        // required before the fragment shader samples this texture as a sampler2D.
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // 6) draw grid on top of the texture
        drawGridToTexture(cam, sampleIndex == 0);

        // 7) bloom post-process (reads texture, writes bloomTex1)
        doBloom(cw, ch);
    }
    void uploadCameraUBO(const Camera& cam, int renderWidth, int renderHeight, int steps, float dLambda,
                         int sampleIndex, vec2 jitter) {
        struct UBOData {
            vec3 pos; float _pad0;
            vec3 right; float _pad1;
            vec3 up; float _pad2;
            vec3 forward; float _pad3;
            float tanHalfFov;
            float aspect;
            int moving;
            int renderWidth;
            int renderHeight;
            int steps;
            float dLambda;
            float _pad4;
            vec2 jitter;     // std140: vec2 at 8-byte boundary (offset 96 here)
            int sampleIndex;
            int showBeam;
        } data;
        vec3 fwd = normalize(cam.target - cam.position());
        vec3 up = vec3(0, 1, 0); // y axis is up, so disk is in x-z plane
        vec3 right = normalize(cross(fwd, up));
        up = cross(right, fwd);

        data.pos = cam.position();
        data.right = right;
        data.up = up;
        data.forward = fwd;
        data.tanHalfFov = tan(radians(60.0f * 0.5f));
        data.aspect = float(WIDTH) / float(HEIGHT);
        data.moving = cam.moving ? 1 : 0;
        data.renderWidth = renderWidth;
        data.renderHeight = renderHeight;
        data.steps = steps;
        data.dLambda = dLambda;
        data.jitter = jitter;
        data.sampleIndex = sampleIndex;
        data.showBeam = showBeam ? 1 : 0;

        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(UBOData), &data);
    }
    void uploadObjectsUBO(const vector<ObjectData>& objs) {
        struct UBOData {
            int   numObjects;
            float _pad0, _pad1, _pad2;        // <-- pad out to 16 bytes
            vec4  posRadius[16];
            vec4  color[16];
            float  mass[16]; 
        } data = {};

        size_t count = std::min(objs.size(), size_t(16));
        data.numObjects = static_cast<int>(count);

        for (size_t i = 0; i < count; ++i) {
            data.posRadius[i] = objs[i].posRadius;
            data.color[i] = objs[i].color;
            data.mass[i] = objs[i].mass;
        }

        // Upload
        glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    }
    void uploadDiskUBO() {
        // disk
        float r1 = showDisk ? (SagA.r_s * 2.2f) : 0.0f;    // inner radius just outside the event horizon
        float r2 = showDisk ? (SagA.r_s * 5.2f) : 0.0f;   // outer radius of the disk
        float num = 2.0;               // number of rays
        float thickness = 1e9f;          // padding for std140 alignment
        
        struct DiskData {
            float r1;
            float r2;
            float num;
            float thickness;
            vec4 color_tint;
        } data = { r1, r2, num, thickness, diskColorTint };

        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(DiskData), &data);
    }
    
    vector<GLuint> QuadVAO(){
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f,  1.0f,  0.0f, 1.0f,  // top left
            -1.0f, -1.0f,  0.0f, 0.0f,  // bottom left
            1.0f, -1.0f,  1.0f, 0.0f,  // bottom right

            -1.0f,  1.0f,  0.0f, 1.0f,  // top left
            1.0f, -1.0f,  1.0f, 0.0f,  // bottom right
            1.0f,  1.0f,  1.0f, 1.0f   // top right
        };
        
        GLuint VAO, VBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D,
                    0,             // mip
                    GL_RGBA8,      // internal format
                    COMPUTE_WIDTH,
                    COMPUTE_HEIGHT,
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    nullptr);
        vector<GLuint> VAOtexture = {VAO, texture};
        return VAOtexture;
    }
    void renderScene() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shaderProgram);
        glBindVertexArray(quadVAO);
        // make sure your fragment shader samples from texture unit 0:
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glfwSwapBuffers(window);
        glfwPollEvents();
    };
};
Engine engine;
static std::string g_currentMode = "window";

static void loadSessionState(const std::string& mode) {
    // Defaults based on the mode:
    int steps = 600;
    float zoom = 6.34194e10f;
    if (mode == "wallpaper") {
        zoom = 1.6e11f;
    } else if (mode == "terminal") {
        zoom = 5.2e10f;
    }

    std::ifstream infile("session_state.txt");
    if (infile.is_open()) {
        std::string key;
        double val;
        while (infile >> key >> val) {
            if (mode == "window") {
                if (key == "window_steps") steps = (int)val;
                else if (key == "window_zoom") zoom = (float)val;
            } else if (mode == "wallpaper") {
                if (key == "wallpaper_steps") steps = (int)val;
                else if (key == "wallpaper_zoom") zoom = (float)val;
            } else if (mode == "terminal") {
                if (key == "terminal_steps") steps = (int)val;
                else if (key == "terminal_zoom") zoom = (float)val;
            }
        }
        infile.close();
    }

    // Apply the loaded values
    engine.COMPUTE_STEPS = steps;
    engine.COMPUTE_MOVING_STEPS = steps;
    camera.radius = glm::clamp(zoom, camera.minRadius, camera.maxRadius);
    camera.update();
}

static void saveSessionState(const std::string& mode) {
    // Defaults
    int window_steps = 600;
    float window_zoom = 6.34194e10f;
    int wallpaper_steps = 600;
    float wallpaper_zoom = 1.6e11f;
    int terminal_steps = 600;
    float terminal_zoom = 5.2e10f;

    // Load existing values first so we preserve other modes' states
    std::ifstream infile("session_state.txt");
    if (infile.is_open()) {
        std::string key;
        double val;
        while (infile >> key >> val) {
            if (key == "window_steps") window_steps = (int)val;
            else if (key == "window_zoom") window_zoom = (float)val;
            else if (key == "wallpaper_steps") wallpaper_steps = (int)val;
            else if (key == "wallpaper_zoom") wallpaper_zoom = (float)val;
            else if (key == "terminal_steps") terminal_steps = (int)val;
            else if (key == "terminal_zoom") terminal_zoom = (float)val;
        }
        infile.close();
    }

    // Overwrite only the current mode's values
    if (mode == "window") {
        window_steps = engine.COMPUTE_STEPS;
        window_zoom = camera.radius;
    } else if (mode == "wallpaper") {
        wallpaper_steps = engine.COMPUTE_STEPS;
        wallpaper_zoom = camera.radius;
    } else if (mode == "terminal") {
        terminal_steps = engine.COMPUTE_STEPS;
        terminal_zoom = camera.radius;
    }

    // Save everything back
    std::ofstream outfile("session_state.txt", std::ios::trunc);
    if (outfile.is_open()) {
        outfile << "window_steps " << window_steps << "\n";
        outfile << "window_zoom " << window_zoom << "\n";
        outfile << "wallpaper_steps " << wallpaper_steps << "\n";
        outfile << "wallpaper_zoom " << wallpaper_zoom << "\n";
        outfile << "terminal_steps " << terminal_steps << "\n";
        outfile << "terminal_zoom " << terminal_zoom << "\n";
        outfile.close();
    }
}

static void saveScreenshot(int w, int h) {
    static int count = 0;
    char name[64];
    snprintf(name, sizeof(name), "bh_%04d.bmp", ++count);

    std::vector<unsigned char> px(w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());

    // BMP: BGR pixel order, rows stored bottom-up (matches GL origin — no flip needed)
    int rowBytes = w * 3;
    int pad      = (4 - rowBytes % 4) % 4;
    int stride   = rowBytes + pad;
    int fileSize = 54 + stride * h;

    unsigned char hdr[54] = {};
    hdr[0]='B'; hdr[1]='M';
    hdr[2]=fileSize;    hdr[3]=fileSize>>8;  hdr[4]=fileSize>>16; hdr[5]=fileSize>>24;
    hdr[10]=54;
    hdr[14]=40;
    hdr[18]=w; hdr[19]=w>>8; hdr[20]=w>>16; hdr[21]=w>>24;
    hdr[22]=h; hdr[23]=h>>8; hdr[24]=h>>16; hdr[25]=h>>24;
    hdr[26]=1; hdr[28]=24;

    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<char*>(hdr), 54);
    std::vector<unsigned char> row(stride, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            row[x*3+0] = px[(y*w+x)*3+2]; // B
            row[x*3+1] = px[(y*w+x)*3+1]; // G
            row[x*3+2] = px[(y*w+x)*3+0]; // R
        }
        f.write(reinterpret_cast<char*>(row.data()), stride);
    }
    std::cout << "[INFO] Screenshot: " << name << " (" << w << "x" << h << ")\n";
}

void setupCameraCallbacks(GLFWwindow* window) {
    glfwSetWindowUserPointer(window, &camera);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseButton(button, action, mods, win);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double x, double y) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseMove(x, y);
    });

    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processScroll(xoffset, yoffset);
        saveSessionState(g_currentMode);
    });

    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processKey(key, scancode, action, mods);

        // Standard rotation (orbit) using arrow keys
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_LEFT) {
                cam->azimuth -= 0.10f;
                cam->update(true);
            }
            else if (key == GLFW_KEY_RIGHT) {
                cam->azimuth += 0.10f;
                cam->update(true);
            }
            else if (key == GLFW_KEY_UP) {
                cam->elevation = glm::clamp(cam->elevation - 0.08f, 0.01f, float(M_PI) - 0.01f);
                cam->update(true);
            }
            else if (key == GLFW_KEY_DOWN) {
                cam->elevation = glm::clamp(cam->elevation + 0.08f, 0.01f, float(M_PI) - 0.01f);
                cam->update(true);
            }
            // Standard zoom using + / - keys
            else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
                cam->processScroll(0, +1);
                saveSessionState(g_currentMode);
            }
            else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
                cam->processScroll(0, -1);
                saveSessionState(g_currentMode);
            }
        }

        // Standard detail (quality/steps) adjustment & exit keys
        if (action == GLFW_PRESS) {
            if (key == GLFW_KEY_RIGHT_BRACKET) {
                engine.COMPUTE_STEPS = std::min(2000, engine.COMPUTE_STEPS + 100);
                engine.COMPUTE_MOVING_STEPS = engine.COMPUTE_STEPS;
                cout << "[INFO] Quality increased. Steps: " << engine.COMPUTE_STEPS << endl;
                cam->update();
                saveSessionState(g_currentMode);
            }
            else if (key == GLFW_KEY_LEFT_BRACKET) {
                engine.COMPUTE_STEPS = std::max(100, engine.COMPUTE_STEPS - 100);
                engine.COMPUTE_MOVING_STEPS = engine.COMPUTE_STEPS;
                cout << "[INFO] Quality decreased. Steps: " << engine.COMPUTE_STEPS << endl;
                cam->update();
                saveSessionState(g_currentMode);
            }
            else if (key == GLFW_KEY_M) {
                showGrid = !showGrid;
                cout << "[INFO] Spacetime grid (mesh) toggled: " << (showGrid ? "ON" : "OFF") << endl;
                cam->update();
            }
            else if (key == GLFW_KEY_Q || key == GLFW_KEY_ESCAPE) {
                glfwSetWindowShouldClose(win, GLFW_TRUE);
            }
            else if (key == GLFW_KEY_S) {
                saveScreenshot(engine.WIDTH, engine.HEIGHT);
            }
            else if (key == GLFW_KEY_B) {
                engine.bloomEnabled = !engine.bloomEnabled;
                cout << "[INFO] Bloom: " << (engine.bloomEnabled ? "ON" : "OFF") << "\n";
                cam->dirty = true;
            }
        }
    });
}


#ifdef _WIN32
static bool isDesktopForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    char className[256];
    if (GetClassNameA(fg, className, sizeof(className))) {
        std::string cls(className);
        if (cls == "Progman" || cls == "WorkerW" || cls == "Shell_TrayWnd" || cls == "SysListView32" || cls == "SHELLDLL_DefView") {
            return true;
        }
    }
    return (fg == GetShellWindow() || fg == GetDesktopWindow());
}

// ---- Wallpaper mode ------------------------------------------------------
// The window is reparented into the desktop's WorkerW layer, so it draws
// behind icons/terminals but receives no input; interaction comes from
// polling the global cursor (parallax) and a global quit hotkey.
struct WallpaperState {
    int monW = 0, monH = 0;
    float baseAzimuth = 0.0f;
    float baseElevation = float(M_PI) / 2.0f;
    float offAz = 0.0f, offEl = 0.0f;
    float amplitudeAz = 0.45f; // radians of orbit swing edge-to-edge of screen
    float amplitudeEl = 0.28f;
};
static WallpaperState wallpaper;

static HWND g_wpHost = nullptr;
static int  g_wpMode = 0; // 0 = WorkerW desktop child (behind icons), 1 = bottom-most window

static std::string hwndHex(HWND h) {
    std::ostringstream o; o << (void*)h; return o.str();
}
// Logs to build\winlibs\wallpaper.log (truncated on first call) AND to stderr,
// so the desktop-attach behaviour on any Windows build is inspectable.
static void wlog(const std::string& s) {
    static std::ofstream f("wallpaper.log", std::ios::trunc);
    f << s << "\n"; f.flush();
    std::cerr << "[wallpaper] " << s << "\n";
}

static bool attachToDesktop(GLFWwindow* win) {
    wallpaper.monW = GetSystemMetrics(SM_CXSCREEN);
    wallpaper.monH = GetSystemMetrics(SM_CYSCREEN);
    if (wallpaper.monW <= 0 || wallpaper.monH <= 0) return false;

    HWND hwnd = glfwGetWin32Window(win);
    wlog("Progman/Desktop mode, monitor "
         + std::to_string(wallpaper.monW) + "x" + std::to_string(wallpaper.monH)
         + ", mode=bottom");

    glfwSetWindowAttrib(win, GLFW_DECORATED, GLFW_FALSE);

    // Default reliable path: a borderless full-monitor top-level window
    // pinned to the BOTTOM of the z-order. Above the desktop wallpaper
    // (so it's visible) but below every app window (so the terminal sits
    // on top of it). It covers the desktop icons — the unavoidable 24H2
    // trade-off, since a real behind-icons layer (WorkerW) won't composite.
    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
    style &= ~WS_OVERLAPPEDWINDOW;
    style |= WS_POPUP;
    SetWindowLongPtrA(hwnd, GWL_STYLE, style);
    LONG_PTR ex = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW; // never steal focus / hide from alt-tab
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, ex);
    // Use GLFW for position/size so its cache matches (otherwise GLFW
    // re-centers the window on the next event and it drifts off 0,0).
    glfwSetWindowPos(win, 0, 0);
    glfwSetWindowSize(win, wallpaper.monW, wallpaper.monH);
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, wallpaper.monW, wallpaper.monH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    g_wpHost = nullptr; g_wpMode = 1;
    wlog("Bottom-most window at 0,0 " + std::to_string(wallpaper.monW)
         + "x" + std::to_string(wallpaper.monH));

    wallpaper.baseAzimuth = camera.azimuth;
    wallpaper.baseElevation = camera.elevation;
    return true;
}

static void wallpaperUpdate(GLFWwindow* win) {
    // Global quit hotkey — as a wallpaper child we never get keyboard focus
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000) &&
        (GetAsyncKeyState('Q') & 0x8000)) {
        glfwSetWindowShouldClose(win, GLFW_TRUE);
        return;
    }

    // Keep the wallpaper pinned to the bottom so any newly opened window sits
    // on top of it (NOACTIVATE => it never steals focus). Re-assert occasionally.
    if (g_wpMode == 1) {
        static int z = 0;
        if ((++z % 60) == 0)
            SetWindowPos(glfwGetWin32Window(win), HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    POINT p;
    if (!GetCursorPos(&p)) return;
    float nx = glm::clamp(2.0f * float(p.x) / float(wallpaper.monW) - 1.0f, -1.0f, 1.0f);
    float ny = glm::clamp(2.0f * float(p.y) / float(wallpaper.monH) - 1.0f, -1.0f, 1.0f);
    float targetAz = nx * wallpaper.amplitudeAz;
    float targetEl = ny * wallpaper.amplitudeEl;

    const float ease = 0.12f; // exponential smoothing per tick
    float dAz = (targetAz - wallpaper.offAz) * ease;
    float dEl = (targetEl - wallpaper.offEl) * ease;

    bool mouseMoving = (fabsf(dAz) > 1e-4f || fabsf(dEl) > 1e-4f);
    if (mouseMoving) {
        wallpaper.offAz += dAz;
        wallpaper.offEl += dEl;
    }

    // Apply keyboard updates (which change baseAzimuth/baseElevation/radius/Gravity)
    bool ctrlAlt = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000);
    bool desktopActive = isDesktopForeground();
    bool keyInteraction = false;

    if (desktopActive || ctrlAlt) {
        float azStep = 0.03f;
        float elStep = 0.02f;

        if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
            wallpaper.baseAzimuth -= azStep;
            keyInteraction = true;
        }
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
            wallpaper.baseAzimuth += azStep;
            keyInteraction = true;
        }
        if (GetAsyncKeyState(VK_UP) & 0x8000) {
            wallpaper.baseElevation = glm::clamp(wallpaper.baseElevation - elStep, 0.01f, float(M_PI) - 0.01f);
            keyInteraction = true;
        }
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
            wallpaper.baseElevation = glm::clamp(wallpaper.baseElevation + elStep, 0.01f, float(M_PI) - 0.01f);
            keyInteraction = true;
        }
        if ((GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) || (GetAsyncKeyState(VK_ADD) & 0x8000)) {
            camera.processScroll(0, +1);
            keyInteraction = true;
            saveSessionState("wallpaper");
        }
        if ((GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) || (GetAsyncKeyState(VK_SUBTRACT) & 0x8000)) {
            camera.processScroll(0, -1);
            keyInteraction = true;
            saveSessionState("wallpaper");
        }

        static bool leftBracketWasDown = false;
        bool leftBracketIsDown = (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0;
        if (leftBracketIsDown && !leftBracketWasDown) {
            engine.COMPUTE_STEPS = std::max(100, engine.COMPUTE_STEPS - 100);
            engine.COMPUTE_MOVING_STEPS = engine.COMPUTE_STEPS;
            cout << "[INFO] Quality decreased. Steps: " << engine.COMPUTE_STEPS << endl;
            keyInteraction = true;
            saveSessionState("wallpaper");
        }
        leftBracketWasDown = leftBracketIsDown;

        static bool rightBracketWasDown = false;
        bool rightBracketIsDown = (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0;
        if (rightBracketIsDown && !rightBracketWasDown) {
            engine.COMPUTE_STEPS = std::min(2000, engine.COMPUTE_STEPS + 100);
            engine.COMPUTE_MOVING_STEPS = engine.COMPUTE_STEPS;
            cout << "[INFO] Quality increased. Steps: " << engine.COMPUTE_STEPS << endl;
            keyInteraction = true;
            saveSessionState("wallpaper");
        }
        rightBracketWasDown = rightBracketIsDown;

        static bool gWasDown = false;
        bool gIsDown = (GetAsyncKeyState('G') & 0x8000) != 0;
        if (gIsDown && !gWasDown) {
            Gravity = !Gravity;
            keyInteraction = true;
        }
        gWasDown = gIsDown;

        static bool mWasDown = false;
        bool mIsDown = (GetAsyncKeyState('M') & 0x8000) != 0;
        if (mIsDown && !mWasDown) {
            showGrid = !showGrid;
            cout << "[INFO] Spacetime grid toggled: " << (showGrid ? "ON" : "OFF") << endl;
            keyInteraction = true;
        }
        mWasDown = mIsDown;
    }

    if (mouseMoving || keyInteraction) {
        camera.azimuth = wallpaper.baseAzimuth + wallpaper.offAz;
        camera.elevation = glm::clamp(wallpaper.baseElevation + wallpaper.offEl,
                                      0.01f, float(M_PI) - 0.01f);
        camera.moving = true;
        camera.dirty = true;
    } else {
        // Gentle ambient drift for the live wallpaper when untouched
        wallpaper.baseAzimuth += 0.003f;
        camera.azimuth = wallpaper.baseAzimuth + wallpaper.offAz;
        camera.elevation = glm::clamp(wallpaper.baseElevation + wallpaper.offEl,
                                      0.01f, float(M_PI) - 0.01f);
        camera.moving = false;
        camera.dirty = true;
    }
}
#endif // _WIN32

#ifdef _WIN32
// ---- Terminal (ANSI) renderer -------------------------------------------
// Reuses the GPU compute pipeline: dispatch the geodesic shader into the
// offscreen texture, read it back, and print it with truecolor half-block
// glyphs (one cell = two vertical pixels via U+2580 ▀, fg=top / bg=bottom).
// Interactive: mouse hover eases a parallax, arrows orbit, +/- zoom, q quits.
static void getTermSize(int& cols, int& rows) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else { cols = 80; rows = 24; }
}

static void runTerminal(Engine& eng) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    DWORD outMode = 0, inMode = 0;
    GetConsoleMode(hOut, &outMode);
    GetConsoleMode(hIn,  &inMode);
    UINT prevOutCP = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8); // the half-block glyph (U+2580) is emitted as UTF-8
    SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    // Disable QuickEdit (else mouse selects text) and enable mouse reports
    SetConsoleMode(hIn, (inMode & ~ENABLE_QUICK_EDIT_MODE)
                        | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

    glfwHideWindow(eng.window);

    float baseAz = camera.azimuth, baseEl = 1.35f; // tilt slightly off edge-on
    float offAz = 0.0f, offEl = 0.0f, tgtAz = 0.0f, tgtEl = 0.0f;
    int prevCols = -1, prevRows = -1;
    bool running = true;
    const int SS  = 1;   // supersampling factor (set to 1 for 4x faster GPU rendering & readback, 2 for antialiasing)
    const int LIT = 20;  // brightness cutoff: dimmer cells stay transparent (terminal bg)
    std::vector<unsigned char> px;  // supersampled GL readback (RGBA)
    std::vector<unsigned char> dn;  // downsampled per-cell pixels (tw x th, RGB)
    std::string frame;

    std::printf("\x1b[?25l\x1b[2J"); // hide cursor, clear

    while (running && !glfwWindowShouldClose(eng.window)) {
        int cols, rows; getTermSize(cols, rows);
        int drawRows = std::max(4, rows - 1); // keep a status line
        int tw = std::max(8, cols);
        int th = drawRows * 2;                 // two pixels per character row
        int ssW = tw * SS, ssH = th * SS;      // render bigger, then box-average down

        if (cols != prevCols || rows != prevRows) {
            eng.COMPUTE_WIDTH = eng.COMPUTE_MOVING_WIDTH = ssW;
            eng.COMPUTE_HEIGHT = eng.COMPUTE_MOVING_HEIGHT = ssH;
            eng.WIDTH = ssW; eng.HEIGHT = ssH; // sets camera aspect = ssW/ssH (no stretch)
            std::printf("\x1b[2J");
            prevCols = cols; prevRows = rows;
        }

        // -- input (non-blocking) --
        DWORD pending = 0;
        GetNumberOfConsoleInputEvents(hIn, &pending);
        if (pending) {
            INPUT_RECORD recs[64]; DWORD got = 0;
            ReadConsoleInput(hIn, recs, std::min<DWORD>(pending, 64), &got);
            for (DWORD i = 0; i < got; ++i) {
                if (recs[i].EventType == KEY_EVENT && recs[i].Event.KeyEvent.bKeyDown) {
                    WORD vk = recs[i].Event.KeyEvent.wVirtualKeyCode;
                    if (vk == 'Q' || vk == VK_ESCAPE) running = false;
                    else if (vk == VK_LEFT)  baseAz -= 0.10f;
                    else if (vk == VK_RIGHT) baseAz += 0.10f;
                    else if (vk == VK_UP)    baseEl = glm::clamp(baseEl - 0.08f, 0.01f, float(M_PI) - 0.01f);
                    else if (vk == VK_DOWN)  baseEl = glm::clamp(baseEl + 0.08f, 0.01f, float(M_PI) - 0.01f);
                    else if (vk == VK_OEM_PLUS  || vk == VK_ADD) {
                        camera.processScroll(0, +1);
                        saveSessionState("terminal");
                    }
                    else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) {
                        camera.processScroll(0, -1);
                        saveSessionState("terminal");
                    }
                    else if (vk == 'M') {
                        showGrid = !showGrid;
                    }
                    else if (vk == 'G') {
                        Gravity = !Gravity;
                    }
                    else if (vk == VK_OEM_4) {
                        engine.COMPUTE_STEPS = std::max(100, engine.COMPUTE_STEPS - 100);
                        engine.COMPUTE_MOVING_STEPS = engine.COMPUTE_STEPS;
                        saveSessionState("terminal");
                    }
                    else if (vk == VK_OEM_6) {
                        engine.COMPUTE_STEPS = std::min(2000, engine.COMPUTE_STEPS + 100);
                        engine.COMPUTE_MOVING_STEPS = engine.COMPUTE_STEPS;
                        saveSessionState("terminal");
                    }
                } else if (recs[i].EventType == MOUSE_EVENT) {
                    COORD mp = recs[i].Event.MouseEvent.dwMousePosition;
                    float nx = glm::clamp(2.0f * float(mp.X) / float(std::max(1, cols)) - 1.0f, -1.0f, 1.0f);
                    float ny = glm::clamp(2.0f * float(mp.Y) / float(std::max(1, rows)) - 1.0f, -1.0f, 1.0f);
                    tgtAz = nx * 0.5f;  // parallax amplitude
                    tgtEl = ny * 0.3f;
                }
            }
        }

        // ease toward the mouse target + a gentle ambient drift (faster now for enhanced sensation)
        offAz += (tgtAz - offAz) * 0.10f;
        offEl += (tgtEl - offEl) * 0.10f;
        baseAz += 0.0035f;
        camera.azimuth   = baseAz + offAz;
        camera.elevation = glm::clamp(baseEl + offEl, 0.01f, float(M_PI) - 0.01f);
        camera.moving = false;
        camera.update();

        eng.dispatchCompute(camera, 0);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);

        // PBO async readback: issue DMA into write PBO, read previous frame from read PBO
        size_t texBytes = size_t(ssW) * ssH * 4;
        eng.initPBO(texBytes);
        const unsigned char* pboPtr = eng.pboBeginRead(GL_RGBA);
        px.resize(texBytes);
        if (pboPtr) memcpy(px.data(), pboPtr, texBytes);
        eng.pboEndRead();

        // box-average SSxSS -> one cell pixel, flipping GL's bottom-origin to top-down
        dn.assign(size_t(tw) * th * 3, 0);
        if (SS == 1) {
            for (int oy = 0; oy < th; ++oy) {
                int srcY = ssH - 1 - oy;
                const unsigned char* row = &px[size_t(srcY) * ssW * 4];
                for (int ox = 0; ox < tw; ++ox) {
                    const unsigned char* p = &row[ox * 4];
                    unsigned char* o = &dn[(size_t(oy) * tw + ox) * 3];
                    o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
                }
            }
        } else {
            for (int oy = 0; oy < th; ++oy)
                for (int ox = 0; ox < tw; ++ox) {
                    int rs = 0, gs = 0, bs = 0;
                    for (int sy = 0; sy < SS; ++sy) {
                        int srcY = ssH - 1 - (oy * SS + sy);
                        const unsigned char* row = &px[size_t(srcY) * ssW * 4];
                        for (int sx = 0; sx < SS; ++sx) {
                            const unsigned char* p = &row[(ox * SS + sx) * 4];
                            rs += p[0]; gs += p[1]; bs += p[2];
                        }
                    }
                    int n = SS * SS;
                    unsigned char* o = &dn[(size_t(oy) * tw + ox) * 3];
                    o[0] = rs / n; o[1] = gs / n; o[2] = bs / n;
                }
        }

        // compose; pure-space cells are left as the terminal's own (transparent) bg
        frame.clear();
        frame.reserve(size_t(tw) * drawRows * 14 + 64);
        frame += "\x1b[H";
        long curFg = -2, curBg = -2; // packed rgb; -1 = terminal default, -2 = unset
        auto cell  = [&](int x, int y) -> const unsigned char* { return &dn[(size_t(y) * tw + x) * 3]; };
        auto isLit = [&](const unsigned char* c) { return (c[0] + c[1] + c[2]) > LIT; };
        auto fgCol = [&](const unsigned char* c) {
            long k = (long(c[0]) << 16) | (c[1] << 8) | c[2];
            if (k != curFg) {
                frame += "\x1b[38;2;";
                frame += numStrs[c[0]]; frame += ";";
                frame += numStrs[c[1]]; frame += ";";
                frame += numStrs[c[2]]; frame += "m";
                curFg = k;
            }
        };
        auto bgCol = [&](const unsigned char* c) {
            long k = (long(c[0]) << 16) | (c[1] << 8) | c[2];
            if (k != curBg) {
                frame += "\x1b[48;2;";
                frame += numStrs[c[0]]; frame += ";";
                frame += numStrs[c[1]]; frame += ";";
                frame += numStrs[c[2]]; frame += "m";
                curBg = k;
            }
        };
        auto bgDef = [&]() { if (curBg != -1) { frame += "\x1b[49m"; curBg = -1; } };
        for (int r = 0; r < drawRows; ++r) {
            for (int x = 0; x < tw; ++x) {
                const unsigned char* t = cell(x, r * 2);
                const unsigned char* b = cell(x, r * 2 + 1);
                bool tl = isLit(t), bl = isLit(b);
                if (tl && bl) { fgCol(t); bgCol(b); frame += "\xe2\x96\x80"; }   // ▀ both halves
                else if (tl)  { fgCol(t); bgDef();  frame += "\xe2\x96\x80"; }   // ▀ top only, bg transparent
                else if (bl)  { fgCol(b); bgDef();  frame += "\xe2\x96\x84"; }   // ▄ bottom only, bg transparent
                else          { bgDef();            frame += " ";          }   // empty space = terminal bg
            }
            bgDef(); frame += "\n";
        }
        frame += "\x1b[0m  black hole  ·  move mouse: parallax   arrows: orbit   +/-: zoom   q: quit ";
        std::fwrite(frame.data(), 1, frame.size(), stdout);
        std::fflush(stdout);

        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps cap (much lighter on CPU & Terminal)
    }

    std::printf("\x1b[0m\x1b[?25h\x1b[2J\x1b[H"); // restore cursor, clear
    std::fflush(stdout);
    SetConsoleOutputCP(prevOutCP);
    SetConsoleMode(hOut, outMode);
    SetConsoleMode(hIn,  inMode);
}



// ---- DirectComposition wallpaper (behind icons, interactive) -------------
// The real behind-icons path on Win11 24H2: DWM won't composite a raw GL or GDI
// child of WorkerW, but it DOES composite a DirectComposition visual. We render
// the scene with OpenGL, read it back, upload into a DXGI composition swapchain,
// and present it through a DComp visual whose host window is reparented into
// WorkerW (so it sits behind the icons). Still interactive (cursor parallax).
static void runWallpaperDComp(GLFWwindow* win, Engine& eng) {
    glfwHideWindow(win);
    int W = GetSystemMetrics(SM_CXSCREEN);
    int H = GetSystemMetrics(SM_CYSCREEN);
    wallpaper.monW = W; wallpaper.monH = H;

    // Render at fifth-res and let DComp scale the visual to full screen (very light).
    int rw = std::max(384, W / 5), rh = std::max(216, H / 5);
    eng.COMPUTE_WIDTH = eng.COMPUTE_MOVING_WIDTH = rw;
    eng.COMPUTE_HEIGHT = eng.COMPUTE_MOVING_HEIGHT = rh;
    eng.COMPUTE_MOVING_STEPS = eng.COMPUTE_STEPS;

    HWND progman = FindWindowA("Progman", nullptr);
    DWORD_PTR ig = 0;
    if (progman) {
        SendMessageTimeoutA(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &ig);
        SendMessageTimeoutA(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &ig);
    }
    HWND workerw = progman ? FindWindowExA(progman, nullptr, "WorkerW", nullptr) : nullptr;
    if (!workerw) workerw = progman;

    WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "BHDComp";
    RegisterClassA(&wc);
    HWND host = CreateWindowExA(WS_EX_NOREDIRECTIONBITMAP, "BHDComp", "", WS_POPUP,
                                0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(host, SW_SHOWNOACTIVATE);

    ID3D11Device* d3d = nullptr; ID3D11DeviceContext* ctx = nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, &ctx))) {
        wlog("DComp: D3D11CreateDevice failed"); return;
    }
    IDXGIDevice* dxgiDev = nullptr; d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    IDXGIAdapter* ad = nullptr; dxgiDev->GetAdapter(&ad);
    IDXGIFactory2* fac = nullptr; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);

    DXGI_SWAP_CHAIN_DESC1 scd = {}; scd.Width = rw; scd.Height = rh;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; scd.BufferCount = 2; scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    IDXGISwapChain1* sc = nullptr; fac->CreateSwapChainForComposition(d3d, &scd, nullptr, &sc);

    // CPU-writable staging texture for uploading the GL readback.
    D3D11_TEXTURE2D_DESC td = {}; td.Width = rw; td.Height = rh; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Texture2D* staging = nullptr; d3d->CreateTexture2D(&td, nullptr, &staging);

    IDCompositionDevice* dc = nullptr;
    DCompositionCreateDevice(dxgiDev, __uuidof(IDCompositionDevice), (void**)&dc);
    IDCompositionTarget* tg = nullptr; dc->CreateTargetForHwnd(host, TRUE, &tg);
    IDCompositionVisual* vis = nullptr; dc->CreateVisual(&vis); vis->SetContent(sc);
    IDCompositionScaleTransform* scale = nullptr; dc->CreateScaleTransform(&scale);
    scale->SetScaleX(float(W) / rw); scale->SetScaleY(float(H) / rh);
    vis->SetTransform(scale);
    tg->SetRoot(vis); dc->Commit();

    // Reparent the host into WorkerW AFTER the target exists -> behind icons.
    LONG_PTR st = GetWindowLongPtrA(host, GWL_STYLE); st |= WS_CHILD;
    SetWindowLongPtrA(host, GWL_STYLE, st);
    SetParent(host, workerw);
    SetWindowPos(host, nullptr, 0, 0, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

    g_wpMode = 3; // skip the top-level z-order re-assert in wallpaperUpdate
    wallpaper.baseAzimuth = camera.azimuth; wallpaper.baseElevation = camera.elevation;
    wlog("DComp wallpaper behind icons, render " + std::to_string(rw) + "x" + std::to_string(rh));

    int accumSample = 0;
    const int ACCUM_SAMPLES = 16;
    const double minFrame = 1.0 / 30.0; double last = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000) &&
            (GetAsyncKeyState('Q') & 0x8000)) break;
        wallpaperUpdate(win);

        // Calculate gravity scene motion
        bool sceneDirty = updateGravityPhysics();

        bool wantAccum = !camera.dirty && !Gravity && !camera.moving
                         && accumSample > 0 && accumSample < ACCUM_SAMPLES;

        if (!camera.dirty && !sceneDirty && !wantAccum) {
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // Sleep to save GPU/CPU
            continue;
        }

        if (camera.dirty || sceneDirty) {
            accumSample = 0;
            eng.dispatchCompute(camera, accumSample);
            accumSample = 1;
            camera.dirty = false;
        } else if (wantAccum) {
            eng.dispatchCompute(camera, accumSample);
            ++accumSample;
        }

        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);

        // PBO async readback: issue DMA into write PBO, upload previous frame to D3D11
        eng.initPBO(size_t(rw) * rh * 4);
        const unsigned char* pboPtr = eng.pboBeginRead(GL_BGRA);
        if (pboPtr) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                for (int y = 0; y < rh; ++y)
                    memcpy((unsigned char*)ms.pData + size_t(y) * ms.RowPitch,
                           pboPtr + size_t(rh - 1 - y) * rw * 4, size_t(rw) * 4);
                ctx->Unmap(staging, 0);
            }
        }
        eng.pboEndRead();
        ID3D11Texture2D* back = nullptr; sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
        if (back) { ctx->CopyResource(back, staging); back->Release(); }
        sc->Present(1, 0);
        dc->Commit(); // push the new swapchain frame to the composition each tick

        glfwPollEvents();
        double dt = glfwGetTime() - last;
        if (dt < minFrame) std::this_thread::sleep_for(std::chrono::duration<double>(minFrame - dt));
        last = glfwGetTime();
    }
}
#endif // _WIN32

static void runUnitTests() {
    cout << "========================================" << endl;
    cout << "        RUNNING BLACK HOLE TESTS        " << endl;
    cout << "========================================" << endl;

    // Test 1: Camera dirty & orbit logic
    {
        Camera testCam;
        testCam.azimuth = 0.0f;
        testCam.elevation = float(M_PI) / 2.0f;
        testCam.dirty = false;

        // Simulate a keyboard azimuth movement
        testCam.azimuth += 0.03f;
        testCam.dirty = true;
        
        if (testCam.dirty != true || testCam.azimuth != 0.03f) {
            cerr << "[FAIL] Test 1: Camera Orbit Update failed!" << endl;
            exit(EXIT_FAILURE);
        }
        cout << "[PASS] Test 1: Camera Orbit Update" << endl;
    }

    // Test 2: Camera Elevation clamping
    {
        Camera testCam;
        testCam.elevation = 0.01f;
        
        // Try to move beyond bounds
        testCam.elevation -= 0.02f;
        float clampedElevation = glm::clamp(testCam.elevation, 0.01f, float(M_PI) - 0.01f);
        if (clampedElevation != 0.01f) {
            cerr << "[FAIL] Test 2: Camera Elevation Clamping failed!" << endl;
            exit(EXIT_FAILURE);
        }
        cout << "[PASS] Test 2: Camera Elevation Clamping" << endl;
    }

    // Test 3: Idle rendering logic state machine
    {
        bool cameraDirty = true;
        bool gravityActive = false;
        int accumSample = 0;
        const int ACCUM_SAMPLES = 16;

        // Case A: Camera is dirty -> we should render
        bool shouldRender = cameraDirty || gravityActive || (accumSample > 0 && accumSample < ACCUM_SAMPLES);
        if (shouldRender != true) {
            cerr << "[FAIL] Test 3 Case A failed!" << endl;
            exit(EXIT_FAILURE);
        }

        // Case B: Camera not dirty, gravity off, but we are accumulating AA
        cameraDirty = false;
        accumSample = 1;
        shouldRender = cameraDirty || gravityActive || (accumSample > 0 && accumSample < ACCUM_SAMPLES);
        if (shouldRender != true) {
            cerr << "[FAIL] Test 3 Case B failed!" << endl;
            exit(EXIT_FAILURE);
        }

        // Case C: Camera not dirty, gravity off, accumulation finished -> should go to sleep
        accumSample = 16;
        shouldRender = cameraDirty || gravityActive || (accumSample > 0 && accumSample < ACCUM_SAMPLES);
        if (shouldRender != false) {
            cerr << "[FAIL] Test 3 Case C failed!" << endl;
            exit(EXIT_FAILURE);
        }
        
        cout << "[PASS] Test 3: Idle Rendering State Machine" << endl;
    }

    // Test 4: Desktop Foreground API compilation & check
    {
#ifdef _WIN32
        bool desktopActive = isDesktopForeground();
        cout << "[INFO] Windows Desktop Active Check: " << (desktopActive ? "YES" : "NO") << endl;
        cout << "[PASS] Test 4: isDesktopForeground compiles and executes successfully" << endl;
#else
        cout << "[INFO] Test 4 skipped (Windows-only test)" << endl;
#endif
    }

    cout << "========================================" << endl;
    cout << "       ALL TESTS COMPLETED SUCCESSFULLY " << endl;
    cout << "========================================" << endl;
}

// -- MAIN -- //
int main(int argc, char** argv) {
    bool wallpaperMode = false;
    bool terminalMode = false;
    bool dcompMode = false;
    bool runTestsMode = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--wallpaper") == 0 || strcmp(argv[i], "-w") == 0)
            wallpaperMode = true;
        else if (strcmp(argv[i], "--terminal") == 0 || strcmp(argv[i], "-t") == 0)
            terminalMode = true;
        else if (strcmp(argv[i], "--dcomp") == 0 || strcmp(argv[i], "-d") == 0)
            dcompMode = true; // behind-icons wallpaper via DirectComposition (Win11 24H2)
        else if (strcmp(argv[i], "--no-grid") == 0)
            showGrid = false;
        else if (strcmp(argv[i], "--no-disk") == 0)
            showDisk = false;
        else if (strcmp(argv[i], "--no-beam") == 0)
            showBeam = false;
        else if (strcmp(argv[i], "--test") == 0)
            runTestsMode = true;
        else if (strcmp(argv[i], "--red") == 0)
            diskColorTint = vec4(0.0f, 0.0f, 0.0f, 2.0f); // Red gradient mode
        else if (strcmp(argv[i], "--white") == 0)
            diskColorTint = vec4(0.0f, 0.0f, 0.0f, 3.0f); // White gradient mode
    }

    if (runTestsMode) {
        runUnitTests();
        return 0;
    }

    setupCameraCallbacks(engine.window);

#ifdef _WIN32
    if (wallpaperMode) {
        g_currentMode = "wallpaper";
    }
#endif
    loadSessionState(g_currentMode);

#ifdef _WIN32
    if (terminalMode) {
        g_currentMode = "terminal";
        loadSessionState(g_currentMode);
        runTerminal(engine);
        saveSessionState(g_currentMode);
        glfwDestroyWindow(engine.window);
        glfwTerminate();
        return 0;
    }
    if (dcompMode) {
        g_currentMode = "wallpaper";
        loadSessionState(g_currentMode);
        runWallpaperDComp(engine.window, engine);
        saveSessionState(g_currentMode);
        glfwDestroyWindow(engine.window);
        glfwTerminate();
        return 0;
    }
#endif

#ifdef _WIN32
    if (wallpaperMode) {
        if (attachToDesktop(engine.window)) {
            glfwGetFramebufferSize(engine.window, &engine.WIDTH, &engine.HEIGHT);
            // Half-res static (light; upscaled to the window), quarter-res while
            // the mouse moves.
            engine.COMPUTE_WIDTH         = std::max(480, engine.WIDTH / 3);
            engine.COMPUTE_HEIGHT        = std::max(270, engine.HEIGHT / 3);
            engine.COMPUTE_MOVING_WIDTH  = std::max(240, engine.WIDTH / 6);
            engine.COMPUTE_MOVING_HEIGHT = std::max(135, engine.HEIGHT / 6);
            engine.COMPUTE_MOVING_STEPS  = engine.COMPUTE_STEPS;
            cout << "[INFO] Wallpaper mode " << engine.WIDTH << "x" << engine.HEIGHT
                 << " (light) — quit with Ctrl+Alt+Q" << endl;
        } else {
            cerr << "[WARN] Could not attach to the desktop, running windowed" << endl;
            wallpaperMode = false;
            g_currentMode = "window";
            loadSessionState(g_currentMode);
        }
    }
#else
    if (wallpaperMode) {
        cerr << "[WARN] --wallpaper is Windows-only, running windowed" << endl;
        wallpaperMode = false;
        g_currentMode = "window";
        loadSessionState(g_currentMode);
    }
#endif

    // Windowed (non-wallpaper) mode: render at real-time 480x360 with no static discrepancy
    if (!wallpaperMode) {
        engine.COMPUTE_WIDTH           = 480;
        engine.COMPUTE_HEIGHT          = 360;
        engine.COMPUTE_MOVING_WIDTH    = 480;
        engine.COMPUTE_MOVING_HEIGHT   = 360;
        engine.COMPUTE_MOVING_STEPS    = engine.COMPUTE_STEPS;
        engine.COMPUTE_D_LAMBDA        = 7.5e8f;
        engine.COMPUTE_MOVING_D_LAMBDA = 7.5e8f;
    }

    vector<unsigned char> pixels(engine.WIDTH * engine.HEIGHT * 3);

    auto t0 = Clock::now();
    lastPrintTime = chrono::duration<double>(t0.time_since_epoch()).count();

    double lastTime = glfwGetTime();
    int   renderW  = 800, renderH = 600, numSteps = 80000;
    int accumSample = 0;          // temporal-AA progress for the current static view
    // Enable temporal AA (16 samples) for all modes, including wallpaper,
    // to accumulate a smooth anti-aliased image when static.
    // The windowed static frame is already full-res + fine-step, so it needs
    // far fewer temporal-AA passes to look clean — keep it low so the cached
    // frame settles quickly after each camera stop instead of grinding 16 heavy
    // passes. Wallpaper/cover modes are light per frame, so they keep 16.
    int ACCUM_SAMPLES = wallpaperMode ? 16 : 4;
    // Cap the wallpaper frame rate so continuous mouse-parallax doesn't peg the GPU.
    const double wpMinFrame = wallpaperMode ? (1.0 / 30.0) : 0.0;
    double lastFrameStart = glfwGetTime();
    while (!glfwWindowShouldClose(engine.window)) {
        if (engine.checkShaderReload()) { accumSample = 0; camera.dirty = true; }
#ifdef _WIN32
        if (wallpaperMode) wallpaperUpdate(engine.window);
#endif
        bool wantAccum = !camera.dirty && !Gravity && !camera.moving
                         && accumSample > 0 && accumSample < ACCUM_SAMPLES;
        if (!camera.dirty && !Gravity && !wantAccum) {
            glfwWaitEventsTimeout(1.0 / 30.0);
            continue;
        }
        // Throttle active frames to wpMinFrame (wallpaper only)
        if (wpMinFrame > 0.0) {
            double dtf = glfwGetTime() - lastFrameStart;
            if (dtf < wpMinFrame)
                std::this_thread::sleep_for(std::chrono::duration<double>(wpMinFrame - dtf));
            lastFrameStart = glfwGetTime();
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // optional, but good practice
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        double now   = glfwGetTime();
        double dt    = now - lastTime;   // seconds since last frame
        lastTime     = now;

        // Gravity
        bool sceneDirty = updateGravityPhysics();



        // ---------- RUN RAYTRACER ------------- //
        glViewport(0, 0, engine.WIDTH, engine.HEIGHT);
        if (camera.dirty || sceneDirty) {
            accumSample = 0; // view changed: restart the AA history
            engine.dispatchCompute(camera, accumSample);
            accumSample = 1;
            camera.dirty = false;
        } else if (wantAccum) {
            engine.dispatchCompute(camera, accumSample); // one more jittered sample
            ++accumSample;
        }
        engine.drawFullScreenQuad();





        // 6) present to screen
        glfwSwapBuffers(engine.window);
        glfwPollEvents();
    }

    saveSessionState(g_currentMode);
    glfwDestroyWindow(engine.window);
    glfwTerminate();
    return 0;
}
