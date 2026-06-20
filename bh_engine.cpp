#include "bh_engine.hpp"

Engine::Engine() {
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    if (!glfwInit()) { cerr << "GLFW init failed\n"; exit(EXIT_FAILURE); }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_ALPHA_BITS, 0);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
    if (!window) { cerr << "Failed to create GLFW window\n"; glfwTerminate(); exit(EXIT_FAILURE); }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        cerr << "GLEW init failed: " << (const char*)glewGetErrorString(glewErr) << "\n";
        glfwTerminate(); exit(EXIT_FAILURE);
    }
    cout << "OpenGL " << glGetString(GL_VERSION) << "\n";
    shaderProgram = CreateShaderProgram();
    gridShaderProgram = CreateShaderProgram("grid.vert", "grid.frag");
    computeProgram = CreateComputeProgram("geodesic.comp");
    try { lastShaderMtime = fs::last_write_time(resourcePath("geodesic.comp")); } catch (...) {}
    initBloom();

    glGenBuffers(1, &cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferData(GL_UNIFORM_BUFFER, 128, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO);

    glGenBuffers(1, &diskUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
    glBufferData(GL_UNIFORM_BUFFER, 32, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO);

    glGenBuffers(1, &objectsUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
    GLsizeiptr objSz = sizeof(int) + 3*sizeof(float) + 16*(sizeof(vec4)+sizeof(vec4)) + 16*sizeof(float);
    glBufferData(GL_UNIFORM_BUFFER, objSz, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO);

    auto res = QuadVAO();
    quadVAO = res[0]; texture = res[1];

    glGenTextures(1, &accumTexture);
    glBindTexture(GL_TEXTURE_2D, accumTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, COMPUTE_WIDTH, COMPUTE_HEIGHT, 0, GL_RGBA, GL_FLOAT, nullptr);
}

void Engine::generateGrid(const vector<ObjectData>& objs) {
    const int gridSize = 25;
    const float spacing = 1e10f;
    vector<vec3> verts;
    vector<GLuint> idx;
    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            float wx = (x - gridSize/2)*spacing, wz = (z - gridSize/2)*spacing, y = 0.0f;
            for (const auto& obj : objs) {
                vec3 op = vec3(obj.posRadius);
                double rs = 2.0*G*obj.mass/(c*c);
                double dx = wx-op.x, dz = wz-op.z, dist = sqrt(dx*dx+dz*dz);
                if (dist > rs) y += float(2.0*sqrt(rs*(dist-rs))) - 3e10f;
                else           y += float(2.0*sqrt(rs*rs)) - 3e10f;
            }
            verts.emplace_back(wx, y, wz);
        }
    }
    for (int z = 0; z < gridSize; ++z)
        for (int x = 0; x < gridSize; ++x) {
            int i = z*(gridSize+1)+x;
            idx.push_back(i); idx.push_back(i+1);
            idx.push_back(i); idx.push_back(i+gridSize+1);
        }
    if (!gridVAO) glGenVertexArrays(1, &gridVAO);
    if (!gridVBO) glGenBuffers(1, &gridVBO);
    if (!gridEBO) glGenBuffers(1, &gridEBO);
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(vec3), verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), nullptr);
    gridIndexCount = (int)idx.size();
    glBindVertexArray(0);
}

void Engine::drawGrid(const mat4& viewProj) {
    glUseProgram(gridShaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "viewProj"), 1, GL_FALSE, value_ptr(viewProj));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accumTexture);
    glUniform1i(glGetUniformLocation(gridShaderProgram, "screenTexture"), 0);
    glBindVertexArray(gridVAO);
    glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawElements(GL_LINES, gridIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0); glEnable(GL_DEPTH_TEST);
}

void Engine::drawGridToTexture(const Camera& cam, bool forceRegen) {
    if (!showGrid) return;
    static bool gridGenerated = false;
    if (!gridGenerated || forceRegen) { generateGrid(objects); gridGenerated = true; }
    if (!gridFBO) glGenFramebuffers(1, &gridFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, gridFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    int cw = cam.moving ? COMPUTE_MOVING_WIDTH : COMPUTE_WIDTH;
    int ch = cam.moving ? COMPUTE_MOVING_HEIGHT : COMPUTE_HEIGHT;
    GLint prevVP[4]; glGetIntegerv(GL_VIEWPORT, prevVP);
    glViewport(0, 0, cw, ch);
    mat4 view = lookAt(cam.position(), cam.target, vec3(0,1,0));
    mat4 proj = perspective(radians(60.0f), float(cw)/ch, 1e9f, 1e14f);
    drawGrid(proj * view);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
}

GLuint Engine::tryCreateComputeProgram(const char* path) {
    std::ifstream in(resourcePath(path)); if (!in.is_open()) return 0;
    std::stringstream ss; ss << in.rdbuf();
    std::string src = ss.str(); const char* s = src.c_str();
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, &s, nullptr); glCompileShader(cs);
    GLint ok; glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len; glGetShaderiv(cs, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len); glGetShaderInfoLog(cs, len, nullptr, log.data());
        std::cerr << "[RELOAD ERROR] " << log.data() << "\n";
        glDeleteShader(cs); return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs); glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len; glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len); glGetProgramInfoLog(prog, len, nullptr, log.data());
        std::cerr << "[RELOAD LINK ERROR] " << log.data() << "\n";
        glDeleteProgram(prog); glDeleteShader(cs); return 0;
    }
    glDeleteShader(cs); return prog;
}

bool Engine::checkShaderReload() {
    try {
        auto mtime = fs::last_write_time(resourcePath(shaderFile));
        if (mtime == lastShaderMtime) return false;
        lastShaderMtime = mtime;
        GLuint p = tryCreateComputeProgram(shaderFile);
        if (p) { glDeleteProgram(computeProgram); computeProgram = p; std::cout << "[RELOAD] " << shaderFile << "\n"; return true; }
    } catch (...) {}
    return false;
}

void Engine::initPBO(size_t size) {
    if (!pbo[0]) glGenBuffers(2, pbo);
    if (pboAllocSize == size) return;
    pboAllocSize = size;
    for (int i = 0; i < 2; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)size, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pboReady = false; pboRead = 0; pboWrite = 1;
}

const unsigned char* Engine::pboBeginRead(GLenum format) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[pboWrite]);
    glGetTexImage(GL_TEXTURE_2D, 0, format, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!pboReady) return nullptr;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[pboRead]);
    return (const unsigned char*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
}

void Engine::pboEndRead() {
    if (pboReady) { glUnmapBuffer(GL_PIXEL_PACK_BUFFER); glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); }
    std::swap(pboRead, pboWrite); pboReady = true;
}

GLuint Engine::createInlineProg(const char* vsSrc, const char* fsSrc) {
    auto compile = [](const char* src, GLenum type) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len); glGetShaderInfoLog(s, len, nullptr, log.data());
            std::cerr << "Shader compile: " << log.data() << "\n";
        }
        return s;
    };
    GLuint vs = compile(vsSrc, GL_VERTEX_SHADER), fs = compile(fsSrc, GL_FRAGMENT_SHADER);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

void Engine::initBloom() {
    const char* qv = R"(#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aTexCoord;
out vec2 TexCoord;
void main(){ gl_Position=vec4(aPos,0,1); TexCoord=aTexCoord; })";

    const char* threshFs = R"(#version 330 core
in vec2 TexCoord; out vec4 FragColor;
uniform sampler2D screenTexture; uniform float threshold;
void main(){
    vec3 c=texture(screenTexture,TexCoord).rgb;
    float lum=dot(c,vec3(0.2126,0.7152,0.0722));
    FragColor=vec4(lum>threshold?c:vec3(0.0),1.0);
})";

    const char* blurFs = R"(#version 330 core
in vec2 TexCoord; out vec4 FragColor;
uniform sampler2D blurTex; uniform vec2 texelSize; uniform bool horizontal;
const float w[5]=float[](0.2270,0.1946,0.1216,0.0541,0.0162);
void main(){
    vec2 dir=horizontal?vec2(texelSize.x,0.0):vec2(0.0,texelSize.y);
    vec3 res=texture(blurTex,TexCoord).rgb*w[0];
    for(int i=1;i<5;i++){
        res+=texture(blurTex,TexCoord+dir*float(i)).rgb*w[i];
        res+=texture(blurTex,TexCoord-dir*float(i)).rgb*w[i];
    }
    FragColor=vec4(res,1.0);
})";
    bloomThreshProg = createInlineProg(qv, threshFs);
    bloomBlurProg   = createInlineProg(qv, blurFs);
    glGenFramebuffers(1, &bloomFBO1); glGenFramebuffers(1, &bloomFBO2);
    glGenTextures(1, &bloomTex1);     glGenTextures(1, &bloomTex2);
    for (GLuint tex : {bloomTex1, bloomTex2}) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
}

void Engine::ensureBloom(int w, int h) {
    int bw = std::max(1, w/2), bh = std::max(1, h/2);
    if (bloomAllocW == bw && bloomAllocH == bh) return;
    bloomAllocW = bw; bloomAllocH = bh;
    auto setup = [&](GLuint fbo, GLuint tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    };
    setup(bloomFBO1, bloomTex1); setup(bloomFBO2, bloomTex2);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Engine::doBloom(int srcW, int srcH) {
    if (!bloomEnabled) return;
    ensureBloom(srcW, srcH);
    int bw = bloomAllocW, bh = bloomAllocH;
    GLint pv[4]; glGetIntegerv(GL_VIEWPORT, pv);
    glViewport(0, 0, bw, bh); glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO1);
    glUseProgram(bloomThreshProg);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(bloomThreshProg, "screenTexture"), 0);
    glUniform1f(glGetUniformLocation(bloomThreshProg, "threshold"), bloomThreshold);
    glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLE_STRIP, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO2);
    glUseProgram(bloomBlurProg);
    glBindTexture(GL_TEXTURE_2D, bloomTex1);
    glUniform1i(glGetUniformLocation(bloomBlurProg, "blurTex"), 0);
    glUniform2f(glGetUniformLocation(bloomBlurProg, "texelSize"), 1.0f/bw, 1.0f/bh);
    glUniform1i(glGetUniformLocation(bloomBlurProg, "horizontal"), GL_TRUE);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO1);
    glBindTexture(GL_TEXTURE_2D, bloomTex2);
    glUniform1i(glGetUniformLocation(bloomBlurProg, "horizontal"), GL_FALSE);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(pv[0], pv[1], pv[2], pv[3]); glEnable(GL_DEPTH_TEST);
}

void Engine::drawFullScreenQuad() {
    glUseProgram(shaderProgram); glBindVertexArray(quadVAO);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomTex1);
    glUniform1i(glGetUniformLocation(shaderProgram, "bloomTexture"), 1);
    glUniform1f(glGetUniformLocation(shaderProgram, "bloomStrength"), bloomEnabled ? bloomStrength : 0.0f);
    glDisable(GL_DEPTH_TEST); glDrawArrays(GL_TRIANGLE_STRIP, 0, 6); glEnable(GL_DEPTH_TEST);
}

GLuint Engine::CreateShaderProgram() {
    const char* vs = R"(#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aTexCoord;
out vec2 TexCoord;
void main(){ gl_Position=vec4(aPos,0.0,1.0); TexCoord=aTexCoord; })";
    const char* fs = R"(#version 330 core
in vec2 TexCoord; out vec4 FragColor;
uniform sampler2D screenTexture; uniform sampler2D bloomTexture; uniform float bloomStrength;
void main(){
    vec3 base=texture(screenTexture,TexCoord).rgb;
    vec3 glow=texture(bloomTexture,TexCoord).rgb;
    FragColor=vec4(base+glow*bloomStrength,1.0);
})";
    return createInlineProg(vs, fs);
}

GLuint Engine::CreateShaderProgram(const char* vertPath, const char* fragPath) {
    auto load = [](const char* path, GLenum type) {
        std::ifstream in(resourcePath(path));
        if (!in.is_open()) { std::cerr << "Cannot open: " << path << "\n"; exit(EXIT_FAILURE); }
        std::stringstream ss; ss << in.rdbuf(); std::string src = ss.str(); const char* s = src.c_str();
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh);
        GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len; glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len); glGetShaderInfoLog(sh, len, nullptr, log.data());
            std::cerr << "Shader error (" << path << "):\n" << log.data() << "\n"; exit(EXIT_FAILURE);
        }
        return sh;
    };
    GLuint vs = load(vertPath, GL_VERTEX_SHADER), fs = load(fragPath, GL_FRAGMENT_SHADER);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len; glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len); glGetProgramInfoLog(prog, len, nullptr, log.data());
        std::cerr << "Link error:\n" << log.data() << "\n"; exit(EXIT_FAILURE);
    }
    glDeleteShader(vs); glDeleteShader(fs); return prog;
}

GLuint Engine::CreateComputeProgram(const char* path) {
    std::ifstream in(resourcePath(path));
    if (!in.is_open()) { std::cerr << "Cannot open compute: " << path << "\n"; exit(EXIT_FAILURE); }
    std::stringstream ss; ss << in.rdbuf(); std::string src = ss.str(); const char* s = src.c_str();
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, &s, nullptr); glCompileShader(cs);
    GLint ok; glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len; glGetShaderiv(cs, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len); glGetShaderInfoLog(cs, len, nullptr, log.data());
        std::cerr << "Compute error:\n" << log.data() << "\n"; exit(EXIT_FAILURE);
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs); glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len; glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len); glGetProgramInfoLog(prog, len, nullptr, log.data());
        std::cerr << "Compute link:\n" << log.data() << "\n"; exit(EXIT_FAILURE);
    }
    glDeleteShader(cs); return prog;
}

void Engine::dispatchCompute(const Camera& cam, int sampleIndex) {
    int cw = cam.moving ? COMPUTE_MOVING_WIDTH  : COMPUTE_WIDTH;
    int ch = cam.moving ? COMPUTE_MOVING_HEIGHT : COMPUTE_HEIGHT;
    int steps = cam.moving ? COMPUTE_MOVING_STEPS : COMPUTE_STEPS;
    float dL  = cam.moving ? COMPUTE_MOVING_D_LAMBDA : COMPUTE_D_LAMBDA;

    glBindTexture(GL_TEXTURE_2D, texture);
    static int aw = 0, ah = 0;
    if (aw != cw || ah != ch) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, accumTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, cw, ch, 0, GL_RGBA, GL_FLOAT, nullptr);
        aw = cw; ah = ch;
    }

    glUseProgram(computeProgram);
    vec2 jitter(0.0f);
    if (sampleIndex > 0) jitter = vec2(halton(sampleIndex,2)-0.5f, halton(sampleIndex,3)-0.5f);
    uploadCameraUBO(cam, cw, ch, steps, dL, sampleIndex, jitter);
    uploadDiskUBO();
    uploadObjectsUBO(objects);

    glBindImageTexture(0, texture,     0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindImageTexture(1, accumTexture,0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glDispatchCompute((GLuint)ceil(cw/16.0f), (GLuint)ceil(ch/16.0f), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    drawGridToTexture(cam, sampleIndex == 0);
    doBloom(cw, ch);
}

void Engine::uploadCameraUBO(const Camera& cam, int rw, int rh, int steps, float dLambda,
                              int sampleIndex, vec2 jitter) {
    struct UBOData {
        vec3 pos; float _p0;
        vec3 right; float _p1;
        vec3 up; float _p2;
        vec3 forward; float _p3;
        float tanHalfFov, aspect;
        int moving, renderWidth, renderHeight, steps;
        float dLambda, kerrSpin;
        vec2 jitter;
        int sampleIndex, showBeam;
        float time;
        int diskAnim;
    } data;
    vec3 fwd = normalize(cam.target - cam.position());
    vec3 up  = vec3(0,1,0);
    vec3 right = normalize(cross(fwd, up));
    up = cross(right, fwd);
    data.pos = cam.position(); data.right = right; data.up = up; data.forward = fwd;
    data.tanHalfFov = tan(radians(60.0f*0.5f));
    data.aspect = float(WIDTH) / float(HEIGHT);
    data.moving = cam.moving ? 1 : 0;
    data.renderWidth = rw; data.renderHeight = rh;
    data.steps = steps; data.dLambda = dLambda;
    data.kerrSpin  = kerrSpin;
    data.jitter = jitter; data.sampleIndex = sampleIndex;
    data.showBeam = showBeam ? 1 : 0;
    data.time = (float)glfwGetTime();
    data.diskAnim = diskAnimEnabled ? 1 : 0;
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(UBOData), &data);
}

void Engine::uploadObjectsUBO(const vector<ObjectData>& objs) {
    struct UBOData {
        int numObjects; float _p0, _p1, _p2;
        vec4 posRadius[16], color[16];
        float mass[16];
    } data = {};
    size_t n = std::min(objs.size(), size_t(16));
    data.numObjects = (int)n;
    for (size_t i = 0; i < n; ++i) {
        data.posRadius[i] = objs[i].posRadius;
        data.color[i]     = objs[i].color;
        data.mass[i]      = objs[i].mass;
    }
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
}

void Engine::uploadDiskUBO() {
    // Inner edge follows the Kerr prograde ISCO. At a*=0 it stays at the artistic
    // 2.2 r_s (the legacy look); as spin rises the ISCO shrinks (rÍSCO/6M ratio),
    // so the disk creeps toward the BH. Clamped just outside the horizon.
    double a = std::clamp((double)kerrSpin, 0.0, 0.998);
    double Z1 = 1.0 + std::cbrt(1.0 - a*a) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    double Z2 = std::sqrt(3.0*a*a + Z1*Z1);
    double iscoM = 3.0 + Z2 - std::sqrt(std::max(0.0, (3.0 - Z1)*(3.0 + Z1 + 2.0*Z2))); // units of M
    double inner = 2.2 * (iscoM / 6.0);                       // r_s units, =2.2 at a=0
    double rplus = 0.5 * (1.0 + std::sqrt(std::max(0.0, 1.0 - a*a)));
    inner = std::max(inner, rplus * 1.1);                     // stay outside the horizon
    float r1 = showDisk ? float(SagA.r_s * inner) : 0.0f;
    float r2 = showDisk ? float(SagA.r_s * 5.2)   : 0.0f;
    struct DiskData { float r1, r2, num, thickness; vec4 color_tint; }
    data = { r1, r2, 2.0f, 1e9f, diskColorTint };
    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(DiskData), &data);
}

vector<GLuint> Engine::QuadVAO() {
    float verts[] = {
        -1.0f,  1.0f, 0.0f, 1.0f,   -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,   -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,    1.0f,  1.0f, 1.0f, 1.0f
    };
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, COMPUTE_WIDTH, COMPUTE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    return {VAO, tex};
}

void Engine::renderScene() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shaderProgram); glBindVertexArray(quadVAO);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glfwSwapBuffers(window); glfwPollEvents();
}
