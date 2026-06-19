#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace glm;
using namespace std;

double c = 299792458.0;
double G = 6.67430e-11;

struct Ray;
extern std::vector<Ray> rays;
void rk4Step(Ray& ray, double dλ, double rs);

// Forward declarations of 2D lensing callbacks
void scrollCallback2D(GLFWwindow* window, double xoffset, double yoffset);
void mouseButtonCallback2D(GLFWwindow* window, int button, int action, int mods);
void cursorPositionCallback2D(GLFWwindow* window, double xpos, double ypos);
void keyCallback2D(GLFWwindow* window, int key, int scancode, int action, int mods);

// --- Structs --- //
struct Engine {
    GLFWwindow* window;
    int WIDTH = 800;
    int HEIGHT = 600;
    float width = 100000000000.0f; // Width of the viewport in meters
    float height = 75000000000.0f; // Height of the viewport in meters

    // Navigation state
    float offsetX = 0.0f, offsetY = 0.0f;
    float zoom = 1.0f;
    bool middleMousePressed = false;
    double lastMouseX = 0.0, lastMouseY = 0;

    Engine() {
        if (!glfwInit()) {
            cerr << "Failed to initialize GLFW" << endl;
            exit(EXIT_FAILURE);
        }
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole Simulation", NULL, NULL);
        if (!window) {
            cerr << "Failed to create GLFW window" << endl;
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) {
            cerr << "Failed to initialize GLEW" << endl;
            glfwDestroyWindow(window);
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glViewport(0, 0, WIDTH, HEIGHT);
        
        glfwSetScrollCallback(window, scrollCallback2D);
        glfwSetMouseButtonCallback(window, mouseButtonCallback2D);
        glfwSetCursorPosCallback(window, cursorPositionCallback2D);
        glfwSetKeyCallback(window, keyCallback2D);
    }

    void run() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        double left   = -width + offsetX;
        double right  =  width + offsetX;
        double bottom = -height + offsetY;
        double top    =  height + offsetY;
        glOrtho(left, right, bottom, top, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }
};
Engine engine;

struct BlackHole {
    vec3 position;
    double mass;
    double radius;
    double r_s;

    BlackHole(vec3 pos, float m) : position(pos), mass(m) {r_s = 2.0 * G * mass / (c*c);}
    void draw() {
        glBegin(GL_TRIANGLE_FAN);
        glColor3f(1.0f, 0.0f, 0.0f);               // Red color for the black hole
        glVertex2f(0.0f, 0.0f);                    // Center
        for(int i = 0; i <= 100; i++) {
            float angle = 2.0f * M_PI * i / 100;
            float x = r_s * cos(angle); // Radius of 0.1
            float y = r_s * sin(angle);
            glVertex2f(x, y);
        }
        glEnd();
    }
};
BlackHole SagA(vec3(0.0f, 0.0f, 0.0f), 8.54e36); // Sagittarius A black hole
struct Ray{
    // -- cartesian coords -- //
    double x;   double y;
    // -- polar coords -- //
    double r;   double phi;
    double dr;  double dphi;
    vector<vec2> trail; // trail of points
    double E, L;             // conserved quantities

    Ray(vec2 pos, vec2 dir) : x(pos.x), y(pos.y), r(sqrt(pos.x * pos.x + pos.y * pos.y)), phi(atan2(pos.y, pos.x)), dr(dir.x), dphi(dir.y) {
        // step 1) get polar coords (r, phi) :
        this->r = sqrt(x*x + y*y);
        this->phi = atan2(y, x);
        // step 2) seed velocities :
        dr = dir.x * cos(phi) + dir.y * sin(phi); // m/s
        dphi  = ( -dir.x * sin(phi) + dir.y * cos(phi) ) / r;
        // step 3) store conserved quantities
        L = r*r * dphi;
        double f = 1.0 - SagA.r_s/r;  
        double dt_dλ = sqrt( (dr*dr)/(f*f) + (r*r*dphi*dphi)/f );
        E = f * dt_dλ;
        // step 4) start trail :
        trail.push_back({x, y});
    }
    void draw(const std::vector<Ray>& rays) {
        // draw current ray positions as points
        glPointSize(2.0f);
        glColor3f(1.0f, 0.0f, 0.0f);
        glBegin(GL_POINTS);
          for (const auto& ray : rays) {
              glVertex2f(ray.x, ray.y);
          }
        glEnd();
    
        // turn on blending for the trails
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(1.0f);
    
        // draw each trail with fading alpha
        for (const auto& ray : rays) {
            size_t N = ray.trail.size();
            if (N < 2) continue;
    
            glBegin(GL_LINE_STRIP);
            for (size_t i = 0; i < N; ++i) {
                // older points (i=0) get alpha≈0, newer get alpha≈1
                float alpha = float(i) / float(N - 1);
                glColor4f(1.0f, 1.0f, 1.0f, std::max(alpha, 0.05f));
                glVertex2f(ray.trail[i].x, ray.trail[i].y);
            }
            glEnd();
        }
    
        glDisable(GL_BLEND);
    }
    void step(double dλ, double rs) {
        // 1) integrate (r,φ,dr,dφ)
        if(r <= rs) return; // stop if inside the event horizon
        if(r > 2.0 * 1e11) return; // stop if escaped far out (initial viewport width was 1e11)
        rk4Step(*this, dλ, rs);

        // 2) convert back to cartesian x,y
        x = r * cos(phi);
        y = r * sin(phi);

        // 3) record the trail
        trail.push_back({ float(x), float(y) });
    }
};
vector<Ray> rays;

// Callback definitions
void scrollCallback2D(GLFWwindow* window, double xoffset, double yoffset) {
    // Zoom factor: zoom in if scroll up
    float factor = (yoffset > 0) ? 0.85f : 1.15f;
    engine.width *= factor;
    engine.height *= factor;
}

void mouseButtonCallback2D(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        
        double left   = -engine.width + engine.offsetX;
        double right  =  engine.width + engine.offsetX;
        double bottom = -engine.height + engine.offsetY;
        double top    =  engine.height + engine.offsetY;
        
        double wx = left + (mx / engine.WIDTH) * (right - left);
        double wy = top - (my / engine.HEIGHT) * (top - bottom);
        
        // Spawn ray starting at this position pointing right
        rays.push_back(Ray(vec2(wx, wy), vec2(c, 0.0f)));
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE || button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            engine.middleMousePressed = true;
            glfwGetCursorPos(window, &engine.lastMouseX, &engine.lastMouseY);
        } else if (action == GLFW_RELEASE) {
            engine.middleMousePressed = false;
        }
    }
}

void cursorPositionCallback2D(GLFWwindow* window, double xpos, double ypos) {
    if (engine.middleMousePressed) {
        double dx = xpos - engine.lastMouseX;
        double dy = ypos - engine.lastMouseY;
        
        double worldDx = (dx / engine.WIDTH) * (2.0f * engine.width);
        double worldDy = (dy / engine.HEIGHT) * (2.0f * engine.height);
        
        engine.offsetX -= worldDx;
        engine.offsetY += worldDy;
        
        engine.lastMouseX = xpos;
        engine.lastMouseY = ypos;
    }
}

void keyCallback2D(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        rays.clear();
        // Spawn default bundle of 20 parallel rays
        for (float y = -engine.height * 0.8f; y <= engine.height * 0.8f; y += engine.height * 0.08f) {
            rays.push_back(Ray(vec2(-engine.width + engine.offsetX, y + engine.offsetY), vec2(c, 0.0f)));
        }
        cout << "[INFO] Reset 2D rays." << endl;
    }
}

void geodesicRHS(const Ray& ray, double rhs[4], double rs) {
    double r    = ray.r;
    double dr   = ray.dr;
    double dphi = ray.dphi;
    double E    = ray.E;

    double f = 1.0 - rs/r;

    // dr/dλ = dr
    rhs[0] = dr;
    // dφ/dλ = dphi
    rhs[1] = dphi;

    // d²r/dλ² from Schwarzschild null geodesic:
    double dt_dλ = E / f;
    rhs[2] = 
        - (rs/(2*r*r)) * f * (dt_dλ*dt_dλ)
        + (rs/(2*r*r*f)) * (dr*dr)
        + (r - rs) * (dphi*dphi);

    // d²φ/dλ² = -2*(dr * dphi) / r
    rhs[3] = -2.0 * dr * dphi / r;
}
void addState(const double a[4], const double b[4], double factor, double out[4]) {
    for (int i = 0; i < 4; i++)
        out[i] = a[i] + b[i] * factor;
}
void rk4Step(Ray& ray, double dλ, double rs) {
    double y0[4] = { ray.r, ray.phi, ray.dr, ray.dphi };
    double k1[4], k2[4], k3[4], k4[4], temp[4];

    geodesicRHS(ray, k1, rs);
    addState(y0, k1, dλ/2.0, temp);
    Ray r2 = ray; r2.r=temp[0]; r2.phi=temp[1]; r2.dr=temp[2]; r2.dphi=temp[3];
    geodesicRHS(r2, k2, rs);

    addState(y0, k2, dλ/2.0, temp);
    Ray r3 = ray; r3.r=temp[0]; r3.phi=temp[1]; r3.dr=temp[2]; r3.dphi=temp[3];
    geodesicRHS(r3, k3, rs);

    addState(y0, k3, dλ, temp);
    Ray r4 = ray; r4.r=temp[0]; r4.phi=temp[1]; r4.dr=temp[2]; r4.dphi=temp[3];
    geodesicRHS(r4, k4, rs);

    ray.r    += (dλ/6.0)*(k1[0] + 2*k2[0] + 2*k3[0] + k4[0]);
    ray.phi  += (dλ/6.0)*(k1[1] + 2*k2[1] + 2*k3[1] + k4[1]);
    ray.dr   += (dλ/6.0)*(k1[2] + 2*k2[2] + 2*k3[2] + k4[2]);
    ray.dphi += (dλ/6.0)*(k1[3] + 2*k2[3] + 2*k3[3] + k4[3]);
}


int main () {
    // Spawn default bundle of 20 parallel rays on launch
    for (float y = -engine.height * 0.8f; y <= engine.height * 0.8f; y += engine.height * 0.08f) {
        rays.push_back(Ray(vec2(-engine.width, y), vec2(c, 0.0f)));
    }

    while(!glfwWindowShouldClose(engine.window)) {
        engine.run();
        SagA.draw();

        for (auto& ray : rays) {
            ray.step(1.0f, SagA.r_s);
        }
        if (!rays.empty()) {
            rays[0].draw(rays); // Draw once for all rays to optimize
        }

        glfwSwapBuffers(engine.window);
        glfwPollEvents();
    }

    return 0;
}
