#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <iostream>
#include <vector>
using namespace std;

struct Camera {
  glm ::vec3 pos{0.0f, 1.3f, 7.0f};  //initial pos of cam
  float yaw   = -90.0f;             //initial dirn (-z)
  float pitch = 0.0f;
  float speed = 3.0f;
  float sens  = 0.12f;  //sensitivity like yaw+=dx*sens
  bool firstMouse = true; //prevent large jump
  double lastX=0.0, lastY=0.0;
  bool mouseCaptured=true; //esc=false
} cam;


void cursorpos(GLFWwindow* w, double x, double y) //w-window handle,x,y-current mouse pos
{
  if(!cam.mouseCaptured) //in esc
  {
    return;
  }
  if(cam.firstMouse)
  { 
    cam.lastX=x; cam.lastY=y; cam.firstMouse=false; 
  }
  float dx=float(x-cam.lastX),dy=float(cam.lastY-y);
  cam.lastX=x; cam.lastY=y;
  cam.yaw+=dx*cam.sens;
  cam.pitch+=dy*cam.sens;
  cam.pitch=glm::clamp(cam.pitch,-90.0f,90.0f);
}

void keys(GLFWwindow* w,int key,int,int action,int)
{
  if(key==GLFW_KEY_ESCAPE && action==GLFW_PRESS)
  {
    cam.mouseCaptured = !cam.mouseCaptured; //esc so locked mode to move or viceverse,next line is to tell this to glfw
    glfwSetInputMode(w,GLFW_CURSOR,cam.mouseCaptured?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
    cam.firstMouse = true;
  }
}

static GLuint compile(GLenum type, const char* src)
{
  GLuint s=glCreateShader(type);  //creates new empty shader object
  glShaderSource(s,1,&src,nullptr); //sends shader code to compile
  glCompileShader(s); //compiles (converts to gpu machine code)
  GLint ok;
  glGetShaderiv(s,GL_COMPILE_STATUS,&ok); //checking cmpilation completed or not
  if(!ok) 
  { 
    GLint len;
    glGetShaderiv(s,GL_INFO_LOG_LENGTH,&len);
    string log(len,'\0');
    glGetShaderInfoLog(s,len,nullptr,log.data());
    cerr<<"Shader error:"<<log<<endl;  //error print
  }
  return s;
}

static GLuint link(GLuint vs,GLuint fs) //shader linking func
{
  GLuint p=glCreateProgram();
  glAttachShader(p,vs);
  glAttachShader(p,fs);
  glLinkProgram(p);
  GLint ok;
  glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok)
  {
    GLint len; glGetProgramiv(p,GL_INFO_LOG_LENGTH,&len);
    string log(len,'\0');
    glGetProgramInfoLog(p,len,nullptr,log.data());
    cerr<<"Link error: "<<log<<std::endl;
  }
  return p;
}

//vert file contains posn
static const char* VERT = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uView, uProj;
void main(){ gl_Position = uProj * uView * uModel * vec4(aPos,1.0); }
)";

//frag contains the (R,G,B,A(alpha))of all pixels
static const char* FRAG = R"(#version 410 core
out vec4 FragColor; uniform vec3 uColor;
void main(){ FragColor = vec4(uColor,1.0); }
)";


struct Mesh
{ 
    GLuint vao=0; //vao is vertex array object-stores how vertex organized
    GLuint vbo=0; //vbo-stores actual vertex ata
    GLsizei count=0; //no. of vertex
    GLenum mode=GL_TRIANGLES; //type of mess rendering
};

static Mesh makeMesh(const vector<float>&verts,GLenum mode=GL_TRIANGLES)
{
  Mesh m;
  m.count=(GLsizei)(verts.size()/3); // divide by 3 as each vertex contain x,y,z
  m.mode=mode; //uses triangle rendering only

  //creating vao,vbo and storing values
  glGenVertexArrays(1,&m.vao); 
  glBindVertexArray(m.vao);
  glGenBuffers(1,&m.vbo);
  glBindBuffer(GL_ARRAY_BUFFER,m.vbo);

  glBufferData(GL_ARRAY_BUFFER,verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
  glBindVertexArray(0);
  return m;
}

int main()
{
  //Window
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow* w1=glfwCreateWindow(1280,720,"room",nullptr,nullptr);
  glfwMakeContextCurrent(w1);
  gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
  glEnable(GL_DEPTH_TEST);

  glfwSetCursorPosCallback(w1,cursorpos);
  glfwSetKeyCallback(w1,keys);
  glfwSetInputMode(w1, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  GLuint vs=compile(GL_VERTEX_SHADER,VERT);
  GLuint fs=compile(GL_FRAGMENT_SHADER,FRAG);
  GLuint prog=link(vs,fs);
  glDeleteShader(vs);
  glDeleteShader(fs);

  float aspect=1280.f/720.f;
  
  //projection matrix (field of view,aspect,closest dist,furthest dist)
  glm::mat4 proj=glm::perspective(glm::radians(70.0f), aspect, 0.1f, 50.0f);

  //Room dimensions 4x2x4, center at y=1)
  auto quad=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d)
  {
    return vector<float>
    {
      a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z,  //triangle abd
      a.x,a.y,a.z,c.x,c.y,c.z,d.x,d.y,d.z   //triangle acd
    };
  };

  Mesh floorM=makeMesh(quad({-2,0,-2},{ 2,0,-2},{ 2,0, 2},{-2,0, 2}));
  Mesh ceilM =makeMesh(quad({-2,2, 2},{ 2,2, 2},{ 2,2,-2},{-2,2,-2}));
  Mesh backM =makeMesh(quad({-2,0, 2},{ 2,0, 2},{ 2,2, 2},{-2,2, 2}));
  Mesh leftM =makeMesh(quad({-2,0,-2},{-2,0, 2},{-2,2, 2},{-2,2,-2}));
  Mesh rightM=makeMesh(quad({ 2,0, 2},{ 2,0,-2},{ 2,2,-2},{ 2,2, 2}));
  //window openingss
  float wx0=-0.5f, wx1=0.5f, wy0=1.0f, wy1=1.8f;
  Mesh fwTop=makeMesh(quad({-2,wy1,-2},{ 2,wy1,-2},{ 2,2,-2},{-2,2,-2}));
  Mesh fwBot=makeMesh(quad({-2,0,-2},{ 2,0,-2},{ 2,wy0,-2},{-2,wy0,-2}));
  Mesh fwLft=makeMesh(quad({-2,wy0,-2},{wx0,wy0,-2},{wx0,wy1,-2},{-2,wy1,-2}));
  Mesh fwRgt=makeMesh(quad({wx1,wy0,-2},{ 2,wy0,-2},{ 2,wy1,-2},{wx1,wy1,-2}));

  //sky(will add light from this)
  Mesh sky  =makeMesh(quad({-1.5f,0.7f,-3},{1.5f,0.7f,-3},{1.5f,2.1f,-3},{-1.5f,2.1f,-3}));

  //drawing lambda per mess
  auto draw=[&](const Mesh& m, glm::vec3 color, const glm::mat4& view)
  {
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog,"uProj"),1,GL_FALSE,glm::value_ptr(proj));
    glUniformMatrix4fv(glGetUniformLocation(prog,"uView"),1,GL_FALSE,glm::value_ptr(view));
    glm::mat4 model(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(prog,"uModel"),1,GL_FALSE,glm::value_ptr(model));
    glUniform3f(glGetUniformLocation(prog,"uColor"), color.r,color.g,color.b);
    glBindVertexArray(m.vao);
    glDrawArrays(m.mode,0,m.count);
  };

  glClearColor(0.65f,0.75f,0.9f,1.0f); //blue color
  double lastTime=glfwGetTime(); //time for animation
  float orbit=0.0f;    //orbit anglge
  bool wire=false;  //true=geometry in wireframes ,false=solid polygons

  while(!glfwWindowShouldClose(w1)) //until window is closed
  {
    double now=glfwGetTime();
    float dt=float(now-lastTime);
    lastTime=now;

    //Toggle wireframe (f&g keys)
    if(glfwGetKey(w1, GLFW_KEY_F)==GLFW_PRESS){ wire=true; }
    if(glfwGetKey(w1, GLFW_KEY_G)==GLFW_PRESS){ wire=false; }
    glPolygonMode(GL_FRONT_AND_BACK, wire? GL_LINE : GL_FILL);

    // Build camera basis
    glm::vec3 front;
    front.x=cos(glm::radians(cam.yaw))*cos(glm::radians(cam.pitch));
    front.y=sin(glm::radians(cam.pitch));
    front.z=sin(glm::radians(cam.yaw))*cos(glm::radians(cam.pitch));
    front=glm::normalize(front);
    glm::vec3 right=glm::normalize(glm::cross(front,{0,1,0}));
    glm::vec3 up=glm::normalize(glm::cross(right, front));

    //Keyboard movement
    if(glfwGetKey(w1, GLFW_KEY_W)==GLFW_PRESS) cam.pos+=front*cam.speed*dt;
    if(glfwGetKey(w1, GLFW_KEY_S)==GLFW_PRESS) cam.pos-=front*cam.speed*dt;
    if(glfwGetKey(w1, GLFW_KEY_A)==GLFW_PRESS) cam.pos-=right*cam.speed*dt;
    if(glfwGetKey(w1, GLFW_KEY_D)==GLFW_PRESS) cam.pos+=right*cam.speed*dt;
    if(glfwGetKey(w1, GLFW_KEY_Q)==GLFW_PRESS) cam.pos-=up*cam.speed*dt;
    if(glfwGetKey(w1, GLFW_KEY_E)==GLFW_PRESS) cam.pos+=up*cam.speed*dt;

    //Quick teleports
    if(glfwGetKey(w1, GLFW_KEY_1)==GLFW_PRESS) cam.pos={0,1.3f,7.0f};
    if(glfwGetKey(w1, GLFW_KEY_2)==GLFW_PRESS) cam.pos={0,1.3f,3.0f};
    if(glfwGetKey(w1, GLFW_KEY_3)==GLFW_PRESS) cam.pos={0,1.3f,0.4f};

    //Orbit preview (hold O)
    if(glfwGetKey(w1,GLFW_KEY_O)==GLFW_PRESS)
    {
      orbit += 0.8f*dt;
      glm::vec3 focus(0.0f,1.0f,0.0f);
      cam.pos = focus + glm::vec3(7.0f*cos(orbit), 2.0f, 7.0f*sin(orbit));
      // point toward focus
      glm::vec3 dir = glm::normalize(focus - cam.pos);
      cam.yaw   = glm::degrees(atan2(dir.z, dir.x)) - 90.0f;
      cam.pitch = glm::degrees(asin(dir.y));
    }
    //view matrix that converts world coordds into camera view
    glm::mat4 view = glm::lookAt(cam.pos, cam.pos + front, up);

    //eerases old frames for new creation
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    // Sky then room
    draw(sky,   {1.2f,1.15f,1.0f}, view);
    draw(floorM,{0.35f,0.35f,0.4f}, view);
    draw(ceilM, {0.35f,0.35f,0.4f}, view);
    draw(backM, {0.4f, 0.4f, 0.45f}, view);
    draw(leftM, {0.4f, 0.4f, 0.45f}, view);
    draw(rightM,{0.4f, 0.4f, 0.45f}, view);
    draw(fwTop, {0.42f,0.42f,0.47f}, view);
    draw(fwBot, {0.42f,0.42f,0.47f}, view);
    draw(fwLft, {0.42f,0.42f,0.47f}, view);
    draw(fwRgt, {0.42f,0.42f,0.47f}, view);

    glfwSwapBuffers(w1);
    glfwPollEvents();
  }

  glfwTerminate();
  return 0;
}
