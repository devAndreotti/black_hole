#pragma once
#include "bh_types.hpp"

struct Engine {
    GLuint gridShaderProgram = 0;
    GLFWwindow* window = nullptr;
    GLuint quadVAO = 0, texture = 0, accumTexture = 0;
    GLuint shaderProgram = 0, computeProgram = 0;
    GLuint cameraUBO = 0, diskUBO = 0, objectsUBO = 0;
    GLuint gridVAO = 0, gridVBO = 0, gridEBO = 0, gridFBO = 0;
    int gridIndexCount = 0;
    // Bloom
    GLuint bloomFBO1 = 0, bloomFBO2 = 0, bloomTex1 = 0, bloomTex2 = 0;
    GLuint bloomThreshProg = 0, bloomBlurProg = 0;
    int bloomAllocW = 0, bloomAllocH = 0;
    bool  bloomEnabled = true;
    float bloomStrength = 0.65f, bloomThreshold = 0.60f;
    // Hot-reload
    fs::file_time_type lastShaderMtime{};
    const char* shaderFile = "geodesic.comp";  // swapped to legacy via --legacy
    // PBO async readback
    GLuint pbo[2] = {0, 0};
    int pboRead = 0, pboWrite = 1;
    bool pboReady = false;
    size_t pboAllocSize = 0;
    // Kerr / animation
    float kerrSpin = 0.0f;
    bool diskAnimEnabled = false;

    int WIDTH = 800, HEIGHT = 600;
    int COMPUTE_WIDTH  = 320, COMPUTE_HEIGHT  = 240;
    int COMPUTE_MOVING_WIDTH = 320, COMPUTE_MOVING_HEIGHT = 240;
    int COMPUTE_STEPS = 600, COMPUTE_MOVING_STEPS = 600;
    float COMPUTE_D_LAMBDA = 7.5e8f, COMPUTE_MOVING_D_LAMBDA = 7.5e8f;
    float width = 100000000000.0f, height = 75000000000.0f;

    Engine();
    void generateGrid(const vector<ObjectData>& objs);
    void drawGrid(const mat4& viewProj);
    void drawGridToTexture(const Camera& cam, bool forceRegen);
    GLuint tryCreateComputeProgram(const char* path);
    bool checkShaderReload();
    void initPBO(size_t size);
    const unsigned char* pboBeginRead(GLenum format);
    void pboEndRead();
    GLuint createInlineProg(const char* vsSrc, const char* fsSrc);
    void initBloom();
    void ensureBloom(int w, int h);
    void doBloom(int srcW, int srcH);
    void drawFullScreenQuad();
    GLuint CreateShaderProgram();
    GLuint CreateShaderProgram(const char* vertPath, const char* fragPath);
    GLuint CreateComputeProgram(const char* path);
    void dispatchCompute(const Camera& cam, int sampleIndex);
    void uploadCameraUBO(const Camera& cam, int rw, int rh, int steps, float dL,
                         int sampleIndex, vec2 jitter);
    void uploadObjectsUBO(const vector<ObjectData>& objs);
    void uploadDiskUBO();
    vector<GLuint> QuadVAO();
    void renderScene();
};

extern Engine engine;
