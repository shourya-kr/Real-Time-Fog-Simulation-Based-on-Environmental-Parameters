#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <cmath> 
using namespace std;

struct Camera {
    glm::vec3 pos{0.0f, 1.3f, 7.0f};  
    float yaw = -90.0f;             
    float pitch = 0.0f;
    float speed = 3.0f; 
    float sens = 0.12f;  
    bool firstMouse = true; 
    double lastX = 0.0, lastY = 0.0;
    bool mouseCaptured = true; 
} cam;

// Scene Settings
float g_fogDensity = 1.0f; 
float g_extinction = 0.1f;  
float g_orbitSpeed = 0.7f; 
int   g_numSamples = 64;

//Global Ambient Dimmer
float g_fogAmbient = 0.02f; 

//Dithering Toggle
bool  g_useDithering = false; 

//Map Analysis Toggle
// 0 = Normal, 1 = Transmission Map, 2 = Depth Map
int   g_showMapMode = 0; 

void cursorpos(GLFWwindow* w, double x, double y) 
{
    if (!cam.mouseCaptured) return;
    if (cam.firstMouse) { 
        cam.lastX = x; cam.lastY = y; cam.firstMouse = false; 
    }
    float dx = float(x - cam.lastX), dy = float(cam.lastY - y);
    cam.lastX = x; cam.lastY = y;
    cam.yaw += dx * cam.sens;
    cam.pitch += dy * cam.sens;
    cam.pitch = glm::clamp(cam.pitch, -89.9f, 89.9f);
}

void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) 
{
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE) {
            cam.mouseCaptured = !cam.mouseCaptured; 
            glfwSetInputMode(w, GLFW_CURSOR, cam.mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            cam.firstMouse = true;
        }
        
        // [TOGGLE 1] Dithering
        if (key == GLFW_KEY_T && action == GLFW_PRESS) {
            g_useDithering = !g_useDithering;
            cout << "x" << (g_useDithering ? "ON" : "OFF") << endl;
        }
        
        // [TOGGLE 2] Map Analysis
        if (key == GLFW_KEY_M && action == GLFW_PRESS) {
            g_showMapMode = (g_showMapMode + 1) % 3;
            string modeName = (g_showMapMode == 0) ? "Normal Render" : (g_showMapMode == 1 ? "Transmission Map (t)" : "Depth Map (d) [Heatmap]");
            cout << "visual " << modeName << endl;
        }

        // [DIMMER CONTROLS]
        if (key == GLFW_KEY_RIGHT_BRACKET) { // ']' key
            g_fogAmbient += 0.01f;
            cout << "Global Dimmer UP: " << g_fogAmbient << endl;
        }
        if (key == GLFW_KEY_LEFT_BRACKET) { // '[' key
            g_fogAmbient -= 0.01f;
            if(g_fogAmbient < 0.0f) g_fogAmbient = 0.0f;
            cout << "Global Dimmer DOWN: " << g_fogAmbient << endl;
        }
    }
}

static GLuint compile(GLenum type, const char* src) 
{
    GLuint s = glCreateShader(type);  
    glShaderSource(s, 1, &src, nullptr); 
    glCompileShader(s); 
    GLint ok; 
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok); 
    if (!ok) {
        GLint len; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        string log(len, '\0');
        glGetShaderInfoLog(s, len, nullptr, log.data());
        cerr << "Shader error:" << log << endl;  
    }
    return s;
}
static GLuint linkProgram(GLuint vs, GLuint fs) 
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; 
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        string log(len, '\0');
        glGetProgramInfoLog(p, len, nullptr, log.data());
        cerr << "Link error: " << log << endl;
    }
    return p;
}
