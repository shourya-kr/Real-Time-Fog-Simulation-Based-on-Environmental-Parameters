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

static const char* SIMPLE_VERT = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uView, uProj;
void main(){ gl_Position = uProj * uView * uModel * vec4(aPos,1.0); }
)";

static const char* SIMPLE_FRAG = R"(#version 410 core
out vec4 FragColor; uniform vec3 uColor;
void main(){ FragColor = vec4(uColor,1.0); }
)";

static const char* DEPTH_VERT = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightVP;
void main() {
    gl_Position = uLightVP * uModel * vec4(aPos, 1.0);
}
)";
static const char* DEPTH_FRAG = R"(#version 410 core
void main() { }
)";

static const char* FOG_VERT = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 uModel, uView, uProj;

out vec3 vPos;
out vec3 vNormal;

void main(){
    vec4 world = uModel * vec4(aPos, 1.0);
    vPos = world.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProj * uView * world;
}
)";

static const char* FOG_FRAG = R"(#version 410 core
out vec4 FragColor;
in vec3 vPos;
in vec3 vNormal;

uniform vec3 uColor;
uniform vec3 uViewPos;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uLightPower;

uniform sampler2D uShadowMap;
uniform mat4 uLightVP;
uniform float uShadowBias;

uniform float uFogDensity;        
uniform float uExtinction;       
uniform int uNumSamples;

// Ambient Dimmer
uniform float uFogAmbient;

uniform float uConeAngleInner;    
uniform float uConeAngleOuter;    
uniform vec3 uWindowCenter;       

// Research Toggles
uniform bool uDither;
uniform int uShowMapMode; // 0=Normal, 1=Transmission, 2=Depth

// heatmap(Blue -> Green -> Red) ---
vec3 jet(float t) {
    return clamp(vec3(1.5) - abs(4.0 * vec3(t) + vec3(-3, -2, -1)), 0.0, 1.0);
}

// Interleaved Gradient Noise
float ign(vec2 uv) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

float attenuate(float dist) {
    return 1.0 / (1.0 + 0.1 * dist + 0.05 * dist * dist);
}

float shadowAtPoint(vec3 p) {
    vec4 lp = uLightVP * vec4(p,1.0);
    lp /= lp.w;
    vec2 uv = lp.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float sampleDepth = lp.z * 0.5 + 0.5;
    float depthTex = texture(uShadowMap, uv).r;
    return (sampleDepth - uShadowBias > depthTex) ? 0.0 : 1.0;
}

vec3 march(vec3 rayStart, vec3 rayDir, float rayLen) {
    vec3 scatteredLight = vec3(0.0);
    float stepSize = rayLen / float(max(uNumSamples,1));
    float currentAttenuation = 1.0;
    vec3 coneAxis = normalize(uWindowCenter - uLightPos);

    // Dither Calculation
    float offset = 0.5;
    if (uDither) {
        offset = ign(gl_FragCoord.xy);
    }

    for (int i = 0; i < uNumSamples; ++i) {
        if (i >= uNumSamples) break; 
        
        float t = stepSize * (float(i) + offset);
        vec3 p = rayStart + rayDir * t;
        
        // 1. Ambient Fog (Global Dimmer)
        vec3 ambientLight = uLightColor * uFogAmbient;
        
        // 2. Direct Spotlight
        vec3 directLight = vec3(0.0);
        vec3 dirFromLight = normalize(p - uLightPos);
        float coneDot = dot(dirFromLight, coneAxis);
        float directLightFactor = smoothstep(uConeAngleOuter, uConeAngleInner, coneDot);

        if (directLightFactor > 0.0) {
            float vis = shadowAtPoint(p); 
            float lightDistance = length(uLightPos - p);
            float atten = attenuate(lightDistance);
            directLight = uLightColor * uLightPower * atten * directLightFactor * vis;
        }

        // 3. Accumulate
        vec3 totalStepLight = (ambientLight + directLight);
        float scattering = uFogDensity * stepSize;
        scatteredLight += totalStepLight * scattering * currentAttenuation;
        
        currentAttenuation *= exp(-scattering);
    }
    return scatteredLight;
}

void main() {
    // 1. Surface Lighting (Ambient term increases with global dimmer)
    vec3 ambient = (0.1 + uFogAmbient) * uColor; 
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor * uColor;
    vec3 surfaceColor = ambient + diffuse;

    // 2. Volumetric Pass
    vec3 rd = normalize(vPos - uViewPos);
    float rayLen = length(vPos - uViewPos); // 'd(x)'
    vec3 fog = march(uViewPos, rd, rayLen);

    // 3. Transmission 't(x)'
    float transmission = exp(-rayLen * uExtinction);

    // 4. Composite
    vec3 finalColor = surfaceColor * transmission + fog;
    
    // --- VISUALIZATION OUTPUT ---
    if (uShowMapMode == 1) {
        // [TRANSMISSION MAP]
        // Stretch contrast using smoothstep to make cubes clearly visible vs walls
        // Low threshold 0.4, High threshold 1.0
        float contrastT = smoothstep(0.4, 1.0, transmission); 
        FragColor = vec4(vec3(contrastT), 1.0);
    } 
    else if (uShowMapMode == 2) {
        // [DEPTH MAP]
        // Rainbow Heatmap Visualization
        float normalizedDepth = clamp(rayLen / 15.0, 0.0, 1.0);
        vec3 heatmap = jet(normalizedDepth);
        FragColor = vec4(heatmap, 1.0);
    } 
    else {
        // Normal Mode
        FragColor = vec4(finalColor, 1.0);
    }
}
)";

struct Mesh {
    GLuint vao = 0; 
    GLuint vbo = 0; 
    GLsizei count = 0; 
    GLenum mode = GL_TRIANGLES; 
};

static Mesh makeMesh(const vector<float>& verts, GLenum mode = GL_TRIANGLES) {
    Mesh m;
    m.count = (GLsizei)(verts.size() / 6); 
    m.mode = mode; 
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    return m;
}

static Mesh makeCube() {
    vector<float> v = {
         0.5f, -0.5f, -0.5f, 1,0,0,  0.5f,  0.5f, -0.5f, 1,0,0,  0.5f,  0.5f,  0.5f, 1,0,0,
         0.5f, -0.5f, -0.5f, 1,0,0,  0.5f,  0.5f,  0.5f, 1,0,0,  0.5f, -0.5f,  0.5f, 1,0,0,
        -0.5f, -0.5f,  0.5f, -1,0,0, -0.5f,  0.5f,  0.5f, -1,0,0, -0.5f,  0.5f, -0.5f, -1,0,0,
        -0.5f, -0.5f,  0.5f, -1,0,0, -0.5f,  0.5f, -0.5f, -1,0,0, -0.5f, -0.5f, -0.5f, -1,0,0,
        -0.5f, 0.5f, -0.5f, 0,1,0,   0.5f, 0.5f, -0.5f, 0,1,0,   0.5f, 0.5f,  0.5f, 0,1,0,
        -0.5f, 0.5f, -0.5f, 0,1,0,   0.5f, 0.5f,  0.5f, 0,1,0,  -0.5f, 0.5f,  0.5f, 0,1,0,
        -0.5f, -0.5f,  0.5f, 0,-1,0,  0.5f, -0.5f,  0.5f, 0,-1,0,  0.5f, -0.5f, -0.5f, 0,-1,0,
        -0.5f, -0.5f,  0.5f, 0,-1,0,  0.5f, -0.5f, -0.5f, 0,-1,0, -0.5f, -0.5f, -0.5f, 0,-1,0,
        -0.5f, -0.5f, 0.5f, 0,0,1,    0.5f, -0.5f, 0.5f, 0,0,1,    0.5f,  0.5f, 0.5f, 0,0,1,
        -0.5f, -0.5f, 0.5f, 0,0,1,    0.5f,  0.5f, 0.5f, 0,0,1,   -0.5f,  0.5f, 0.5f, 0,0,1,
         0.5f, -0.5f,-0.5f, 0,0,-1,  -0.5f, -0.5f,-0.5f, 0,0,-1,  -0.5f,  0.5f,-0.5f, 0,0,-1,
         0.5f, -0.5f,-0.5f, 0,0,-1,  -0.5f,  0.5f,-0.5f, 0,0,-1,   0.5f,  0.5f,-0.5f, 0,0,-1
    };
    return makeMesh(v);
}

int main() {
    if (!glfwInit()) { cerr << "Failed to init GLFW\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* w1 = glfwCreateWindow(1280, 720, "Fog Engine (T=Dither, M=Map Analysis, []=Dimmer)", nullptr, nullptr);
    if (!w1) { cerr << "Failed create window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(w1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { cerr << "Failed to init GLAD\n"; return -1; }

    glEnable(GL_DEPTH_TEST);
    glfwSetCursorPosCallback(w1, cursorpos);
    glfwSetKeyCallback(w1, key_callback); 
    glfwSetInputMode(w1, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    GLuint simpleVS = compile(GL_VERTEX_SHADER, SIMPLE_VERT);
    GLuint simpleFS = compile(GL_FRAGMENT_SHADER, SIMPLE_FRAG);
    GLuint simpleProg = linkProgram(simpleVS, simpleFS);
    glDeleteShader(simpleVS); glDeleteShader(simpleFS);

    GLuint depthVS = compile(GL_VERTEX_SHADER, DEPTH_VERT);
    GLuint depthFS = compile(GL_FRAGMENT_SHADER, DEPTH_FRAG);
    GLuint depthProg = linkProgram(depthVS, depthFS);
    glDeleteShader(depthVS); glDeleteShader(depthFS);

    GLuint fogVS = compile(GL_VERTEX_SHADER, FOG_VERT);
    GLuint fogFS = compile(GL_FRAGMENT_SHADER, FOG_FRAG);
    GLuint fogProg = linkProgram(fogVS, fogFS);
    glDeleteShader(fogVS); glDeleteShader(fogFS);

    float aspect = 1280.f / 720.f;
    glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 50.0f);

    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 normal) {
        return vector<float>{
            a.x,a.y,a.z, normal.x, normal.y, normal.z, 
            b.x,b.y,b.z, normal.x, normal.y, normal.z,
            c.x,c.y,c.z, normal.x, normal.y, normal.z,
            a.x,a.y,a.z, normal.x, normal.y, normal.z, 
            c.x,c.y,c.z, normal.x, normal.y, normal.z,
            d.x,d.y,d.z, normal.x, normal.y, normal.z
        };
    };

    Mesh floorM = makeMesh(quad({-2,0,-2},{2,0,-2},{2,0,2},{-2,0,2}, {0,1,0}));
    Mesh backM  = makeMesh(quad({-2,0,2},{2,0,2},{2,2,2},{-2,2,2}, {0,0,-1}));
    Mesh leftM  = makeMesh(quad({-2,0,-2},{-2,0,2},{-2,2,2},{-2,2,-2}, {1,0,0}));
    Mesh rightM = makeMesh(quad({2,0,2},{2,0,-2},{2,2,-2},{2,2,2}, {-1,0,0}));
    
    float cx0=-0.5f, cx1=0.5f, cz0=-0.5f, cz1=0.5f;
    glm::vec3 ceilingNormal = {0,-1,0};
    Mesh cwTop = makeMesh(quad({-2,2,cz1},{ 2,2,cz1},{ 2,2,2},{-2,2,2}, ceilingNormal));
    Mesh cwBot = makeMesh(quad({-2,2,-2},{ 2,2,-2},{ 2,2,cz0},{-2,2,cz0}, ceilingNormal));
    Mesh cwLft = makeMesh(quad({-2,2,cz0},{-2,2,cz1},{cx0,2,cz1},{cx0,2,cz0}, ceilingNormal));
    Mesh cwRgt = makeMesh(quad({cx1,2,cz0},{cx1,2,cz1},{2,2,cz1},{2,2,cz0}, ceilingNormal));
    Mesh sky   = makeMesh(quad({cx0,1.99f,cz0},{cx1,1.99f,cz0},{cx1,1.99f,cz1},{cx0,1.99f,cz1}, {0,1,0}));

    Mesh cubeMesh = makeCube();
    vector<glm::mat4> cubeModels;
    vector<glm::vec3> cubeColors;
    struct Cfg { glm::vec3 pos; float size; glm::vec3 color; };
    vector<Cfg> cfgs = {
        {{-1.0f, 0.0f, -1.0f}, 0.5f, {0.7f,0.4f,0.3f}},
        {{ 1.2f, 0.0f,  0.5f}, 0.5f, {0.3f,0.7f,0.4f}},
        {{-0.5f, 0.0f,  0.8f}, 0.4f, {0.5f,0.5f,0.8f}},
        {{ 0.7f, 0.0f, -1.2f}, 0.4f, {0.9f,0.9f,0.5f}},
        {{ 0.2f, 0.0f, -0.2f}, 0.5f, {0.6f,0.4f,0.8f}}
    };

    for (auto &c : cfgs) {
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(c.pos.x, c.size / 2.0f, c.pos.z));
        model = glm::scale(model, glm::vec3(c.size)); 
        cubeModels.push_back(model);
        cubeColors.push_back(c.color);
    }
 
    glm::vec3 lightPos = {0.0f, 1.99f, 0.0f}; 
    glm::vec3 lightColor = {1.0f, 0.9f, 0.7f};
    float lightPower = 4.5f;

    glm::vec3 rotatingTarget = {0.0f, 0.0f, 0.0f};
    float orbitRadius = 0.5f;
    float extinctionCoeff = 0.1f; 

    float coneInnerCos = 0.970f; 
    float coneOuterCos = 0.95f;  

    const int SHADOW_RES = 1024;
    GLuint shadowFBO = 0;
    GLuint shadowTex = 0;
    glGenFramebuffers(1, &shadowFBO);
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        cerr << "Shadow FBO incomplete\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    auto drawMesh = [&](const Mesh& m, GLuint prog, glm::vec3 color, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& model = glm::mat4(1.0f)) {
        glUseProgram(prog);
        GLint locM = glGetUniformLocation(prog, "uModel");
        GLint locV = glGetUniformLocation(prog, "uView");
        GLint locP = glGetUniformLocation(prog, "uProj");
        if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, glm::value_ptr(model));
        if (locV >= 0) glUniformMatrix4fv(locV, 1, GL_FALSE, glm::value_ptr(view));
        if (locP >= 0) glUniformMatrix4fv(locP, 1, GL_FALSE, glm::value_ptr(projection));
        GLint locC = glGetUniformLocation(prog, "uColor");
        if (locC >= 0) glUniform3f(locC, color.r, color.g, color.b);
        glBindVertexArray(m.vao);
        glDrawArrays(m.mode, 0, m.count);
    };

    double lastTime = glfwGetTime(); 
    bool wire = false; 

    while (!glfwWindowShouldClose(w1)) 
    {
        double now = glfwGetTime();
        float dt = float(now-lastTime);
        lastTime=now;

        if (glfwGetKey(w1, GLFW_KEY_F) == GLFW_PRESS) wire = true;
        if (glfwGetKey(w1, GLFW_KEY_G) == GLFW_PRESS) wire = false;
        glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);

        glm::vec3 front;
        front.x = cos(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch));
        front.y = sin(glm::radians(cam.pitch));
        front.z = sin(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch));
        front = glm::normalize(front);
        glm::vec3 right = glm::normalize(glm::cross(front, {0,1,0}));
        glm::vec3 up = glm::normalize(glm::cross(right, front));

        if (glfwGetKey(w1, GLFW_KEY_W) == GLFW_PRESS) cam.pos += front * cam.speed * dt;
        if (glfwGetKey(w1, GLFW_KEY_S) == GLFW_PRESS) cam.pos -= front * cam.speed * dt;
        if (glfwGetKey(w1, GLFW_KEY_A) == GLFW_PRESS) cam.pos -= right * cam.speed * dt;
        if (glfwGetKey(w1, GLFW_KEY_D) == GLFW_PRESS) cam.pos += right * cam.speed * dt;
        if (glfwGetKey(w1, GLFW_KEY_Q) == GLFW_PRESS) cam.pos -= up * cam.speed * dt;
        if (glfwGetKey(w1, GLFW_KEY_E) == GLFW_PRESS) cam.pos += up * cam.speed * dt;

        if (glfwGetKey(w1, GLFW_KEY_1) == GLFW_PRESS) cam.pos = {0,1.3f,7.0f};
        if (glfwGetKey(w1, GLFW_KEY_2) == GLFW_PRESS) cam.pos = {0,1.3f,3.0f};
        if (glfwGetKey(w1, GLFW_KEY_3) == GLFW_PRESS) cam.pos = {0,1.3f,0.4f};

        glm::mat4 view = glm::lookAt(cam.pos, cam.pos + front, up);

        float timeF = (float)now;
        float targetX = sin(timeF * g_orbitSpeed) * orbitRadius;
        float targetZ = cos(timeF * g_orbitSpeed) * orbitRadius;
        rotatingTarget = {targetX, 0.0f, targetZ};

        glm::mat4 lightProj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 50.0f);
        glm::mat4 lightView = glm::lookAt(lightPos, rotatingTarget, glm::vec3(0,1,0));
        glm::mat4 lightVP = lightProj * lightView;

        glViewport(0, 0, SHADOW_RES, SHADOW_RES);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);

        glUseProgram(depthProg);
        GLint d_locLightVP = glGetUniformLocation(depthProg, "uLightVP");
        glUniformMatrix4fv(d_locLightVP, 1, GL_FALSE, glm::value_ptr(lightVP));

        auto drawDepth = [&](const Mesh& m, const glm::mat4& model) {
            GLint locM = glGetUniformLocation(depthProg, "uModel");
            if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, glm::value_ptr(model));
            glBindVertexArray(m.vao);
            glDrawArrays(m.mode, 0, m.count);
        };

        glm::mat4 identity = glm::mat4(1.0f);
        drawDepth(floorM, identity);
        drawDepth(cwTop, identity); drawDepth(cwBot, identity); drawDepth(cwLft, identity); drawDepth(cwRgt, identity);
        drawDepth(backM, identity); drawDepth(leftM, identity); drawDepth(rightM, identity);
        drawDepth(sky, identity);
        
        for (auto &m : cubeModels) {
            drawDepth(cubeMesh, m);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        int winW, winH;
        glfwGetFramebufferSize(w1, &winW, &winH);
        glViewport(0, 0, winW, winH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        drawMesh(sky, simpleProg, lightColor, view, proj);

        glUseProgram(fogProg);
        GLint locProj = glGetUniformLocation(fogProg, "uProj");
        GLint locView = glGetUniformLocation(fogProg, "uView");
        if (locProj >= 0) glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(proj));
        if (locView >= 0) glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));

        GLint locViewPos = glGetUniformLocation(fogProg, "uViewPos");
        GLint locLightPos = glGetUniformLocation(fogProg, "uLightPos");
        GLint locLightColor = glGetUniformLocation(fogProg, "uLightColor");
        GLint locLightPower = glGetUniformLocation(fogProg, "uLightPower");
        GLint locFogDensity = glGetUniformLocation(fogProg, "uFogDensity");
        GLint locExtinction = glGetUniformLocation(fogProg, "uExtinction");
        GLint locNumSamples = glGetUniformLocation(fogProg, "uNumSamples");
        GLint locConeInner = glGetUniformLocation(fogProg, "uConeAngleInner");
        GLint locConeOuter = glGetUniformLocation(fogProg, "uConeAngleOuter");
        GLint locWindowCenter = glGetUniformLocation(fogProg, "uWindowCenter");
        GLint locLightVP = glGetUniformLocation(fogProg, "uLightVP");
        GLint locShadowBias = glGetUniformLocation(fogProg, "uShadowBias");
        
        // Send Toggles
        GLint locDither = glGetUniformLocation(fogProg, "uDither");
        if (locDither >= 0) glUniform1i(locDither, g_useDithering);
        
        //Send Map Mode
        GLint locShowMapMode = glGetUniformLocation(fogProg, "uShowMapMode");
        if (locShowMapMode >= 0) glUniform1i(locShowMapMode, g_showMapMode);

        //Send Ambient
        GLint locFogAmbient = glGetUniformLocation(fogProg, "uFogAmbient");
        if(locFogAmbient >= 0) glUniform1f(locFogAmbient, g_fogAmbient);

        if (locViewPos >= 0) glUniform3fv(locViewPos, 1, glm::value_ptr(cam.pos));
        if (locLightPos >= 0) glUniform3fv(locLightPos, 1, glm::value_ptr(lightPos));
        if (locLightColor >= 0) glUniform3fv(locLightColor, 1, glm::value_ptr(lightColor));
        if (locLightPower >= 0) glUniform1f(locLightPower, lightPower);
        if (locFogDensity >= 0) glUniform1f(locFogDensity, g_fogDensity); 
        if (locExtinction >= 0) glUniform1f(locExtinction, extinctionCoeff);
        if (locNumSamples >= 0) glUniform1i(locNumSamples, g_numSamples); 
        if (locConeInner >= 0) glUniform1f(locConeInner, coneInnerCos);
        if (locConeOuter >= 0) glUniform1f(locConeOuter, coneOuterCos);
        if (locWindowCenter >= 0) glUniform3fv(locWindowCenter, 1, glm::value_ptr(rotatingTarget));
        if (locLightVP >= 0) glUniformMatrix4fv(locLightVP, 1, GL_FALSE, glm::value_ptr(lightVP));
        if (locShadowBias >= 0) glUniform1f(locShadowBias, 0.005f);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, shadowTex);
        GLint locShadowSampler = glGetUniformLocation(fogProg, "uShadowMap");
        if (locShadowSampler >= 0) glUniform1i(locShadowSampler, 3);

        auto drawFog = [&](const Mesh& m, const glm::vec3& color, const glm::mat4& model =glm::mat4(1.0f)) {
            GLint locM = glGetUniformLocation(fogProg, "uModel");
            if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, glm::value_ptr(model));
            GLint locC = glGetUniformLocation(fogProg, "uColor");
            if (locC >= 0) glUniform3f(locC, color.r, color.g, color.b);
            glBindVertexArray(m.vao);
            glDrawArrays(m.mode, 0, m.count);
        };

        drawFog(floorM, {0.35f,0.35f,0.4f});
        drawFog(cwTop, {0.35f,0.35f,0.4f});
        drawFog(cwBot, {0.35f,0.35f,0.4f});
        drawFog(cwLft, {0.35f,0.35f,0.4f});
        drawFog(cwRgt, {0.35f,0.35f,0.4f});
        drawFog(backM, {0.4f,0.4f,0.45f});
        drawFog(leftM, {0.4f,0.4f,0.45f});
        drawFog(rightM,{0.4f,0.4f,0.45f});

        for (size_t i = 0; i < cubeModels.size(); ++i) {
            drawFog(cubeMesh, cubeColors[i], cubeModels[i]);
        }

        glfwSwapBuffers(w1);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
