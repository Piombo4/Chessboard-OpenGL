#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
// GLAD
#include <glad/glad.h>
// GLFW
#include <GLFW/glfw3.h>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Mesh.hpp"
#include "Model.hpp"

// ---------------------------------------------------
// Global variables
GLFWwindow* gWindow = nullptr;
int         gWindowWidth = 1280;
int         gWindowHeight = 720;
int materialBlack = 1; 
int materialWhite = 1;
const int maxMaterialsPieces = 3;
int materialBlackSquares = 1; 
int materialWhiteSquares = 1;
int materialBase = 1; 
const int maxMaterials = 2; 


// Matrices
glm::mat4   gView;
glm::mat4   gProjection;

// For orbiting around the chessboard:
float yaw = 0.0f;    // horizontal angle (in degrees)
float pitch = 20.0f;   // vertical angle   (in degrees)
float radius = 8.0f;    // distance from orbit center
bool  rightClickPressed = false;


glm::vec3 cameraOffset = glm::vec3(0.0f, 3.0f, 0.0f);

// Time
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Mouse tracking
double lastX = 0.0;
double lastY = 0.0;

// Zoom speed for the scroll
float zoomSpeed = 1.0f;

// The orbit center
glm::vec3 orbitCenter(0.f, 0.f, 0.f);


static const char* vsSrc = R".(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 Normal;
out vec2 TexCoords;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    Normal    = aNormal;
    TexCoords = aTexCoords;
}
).";
static const char* fsSrc2 = R".(
#version 330 core

in vec2 TexCoords; // Texture coordinates from vertex shader
in vec3 Normal;    // Normal for lighting calculations

out vec4 FragColor; // Output color
uniform vec3 cameraDir;
uniform int uMaterialID; // Determines which material to use
uniform int uMaterialBlack;
uniform int uMaterialWhite;
uniform int uMaterialWhiteSquares;
uniform int uMaterialBlackSquares;
uniform int uMaterialBoard;


//Marble 
float hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float turbulence(vec2 p) {
    float t = 0.0;
    float scale = 2; // Higher frequency
    for (int i = 0; i < 7; i++) { // More octaves
        t += abs(noise(p * scale) - 0.5) / scale;
        scale *= 2.5; // Increase scaling factor
    }
    return t;
}

float marbleVeins(vec2 p) {
    float t = turbulence(p);
    float veins = sin(p.x * 2 + t * 80.0); // Adjust frequency and amplitude
    veins = smoothstep(0.9, 0.95, veins);  // Narrower range for sparse veins
    return veins;
}

vec3 marbleColor(vec2 uv, vec3 baseColor, vec3 veinColor) {
    float veins = marbleVeins(uv);
    return mix(baseColor, veinColor, veins); // Reduce vein intensity
}


// Wood pattern
#define sat(x) clamp(x, 0., 1.)
#define S(a, b, c) smoothstep(a, b, c)
#define S01(a) S(0., 1., a)

float sum2(vec2 v) { return dot(v, vec2(1)); }

///////////////////////////////////////////////////////////////////////////////

float h31(vec3 p3) {
	p3 = fract(p3 * .1031);
	p3 += dot(p3, p3.yzx + 333.3456);
	return fract(sum2(p3.xy) * p3.z);
}

float h21(vec2 p) { return h31(p.xyx); }

float n31(vec3 p) {
	const vec3 s = vec3(7, 157, 113);

	// Thanks Shane - https://www.shadertoy.com/view/lstGRB
	vec3 ip = floor(p);
	p = fract(p);
	p = p * p * (3. - 2. * p);
	vec4 h = vec4(0, s.yz, sum2(s.yz)) + dot(ip, s);
	h = mix(fract(sin(h) * 43758.545), fract(sin(h + s.x) * 43758.545), p.x);
	h.xy = mix(h.xz, h.yw, p.y);
	return mix(h.x, h.y, p.z);
}

// roughness: (0.0, 1.0], default: 0.5
// Returns unsigned noise [0.0, 1.0]
float fbm(vec3 p, int octaves, float roughness) {
	float sum = 0.,
	      amp = 1.,
	      tot = 0.;
	roughness = sat(roughness);
	for (int i = 0; i < octaves; i++) {
		sum += amp * n31(p);
		tot += amp;
		amp *= roughness;
		p *= 2.;
	}
	return sum / tot;
}

vec3 randomPos(float seed) {
	vec4 s = vec4(seed, 0, 1, 2);
	return vec3(h21(s.xy), h21(s.xz), h21(s.xw)) * 1e2 + 1e2;
}

// Returns unsigned noise [0.0, 1.0]
float fbmDistorted(vec3 p) {
	p += (vec3(n31(p + randomPos(0.)), n31(p + randomPos(1.)), n31(p + randomPos(2.))) * 2. - 1.) * 1.12;
	return fbm(p, 8, .5);
}

// vec3: detail(/octaves), dimension(/inverse contrast), lacunarity
// Returns signed noise.
float musgraveFbm(vec3 p, float octaves, float dimension, float lacunarity) {
	float sum = 0.,
	      amp = 1.,
	      m = pow(lacunarity, -dimension);
	for (float i = 0.; i < octaves; i++) {
		float n = n31(p) * 2. - 1.;
		sum += n * amp;
		amp *= m;
		p *= lacunarity;
	}
	return sum;
}

// Wave noise along X axis.
vec3 waveFbmX(vec3 p) {
	float n = p.x * 20.;
	n += .4 * fbm(p * 3., 3, 3.);
	return vec3(sin(n) * .5 + .5, p.yz);
}

///////////////////////////////////////////////////////////////////////////////
// Math
float remap01(float f, float in1, float in2) { return sat((f - in1) / (in2 - in1)); }

///////////////////////////////////////////////////////////////////////////////
// Wood material.
vec3 matWood(vec3 p, vec3 baseColor, vec3 midColor, vec3 highlightColor) {
    float n1 = fbmDistorted(p * vec3(7.8, 1.17, 1.17));
    n1 = mix(n1, 1., .2);
    float n2 = mix(musgraveFbm(vec3(n1 * 4.6), 8., 0., 2.5), n1, .85),
          dirt = 1. - musgraveFbm(waveFbmX(p * vec3(.01, .15, .15)), 15., .26, 2.4) * .4;
    float grain = 1. - S(.2, 1., musgraveFbm(p * vec3(500, 6, 1), 2., 2., 2.5)) * .2;
    n2 *= dirt * grain;
    
    // Use the provided colors for the wood texture
    return mix(mix(baseColor, midColor, remap01(n2, .19, .56)), highlightColor, remap01(n2, .56, 1.));
}


// Main material logic
void main() {
    vec2 uv = TexCoords; // Scale texture coordinates
    vec3 color = vec3(1.0);     // Default white color
     if (uMaterialID == 1) {
switch(uMaterialWhite){
       case 1: 
         color =vec3(0.75,0.75,0.75);
        break;
        case 2: 
         vec3 baseColor = vec3(0.9);          // White marble base
        vec3 veinColor = vec3(0.3, 0.3, 0.3); // Dark gray veins
         uv *=15;
        color = marbleColor(uv, baseColor, veinColor);
        break;
case 3:
uv *=2;
vec3 p = vec3((uv - 0.5) * 2.0, floor(mod(0, 8.0)));
vec3 woodColor = pow(matWood(p, 
    vec3(0.40, 0.32, 0.20),  // Base color: Più scuro
    vec3(0.55, 0.44, 0.28),  // Mid-tone color: Più scuro del colore principale
    vec3(0.70, 0.56, 0.36)   // Highlight color: Riflessi più scuri
), vec3(.4545));
 color = woodColor;
break;
    }}
    else if (uMaterialID == 2) {
       /* // Black material*/
      switch(uMaterialBlack){
       case 1: 
         color =vec3(0.25,0.25,0.25);
        break;
        case 2: 
        vec3 baseColor = vec3(0.25);         
        vec3 veinColor = vec3(0.8, 0.8, 0.8); 
        uv *=15;
        color = marbleColor(uv, baseColor, veinColor);
        break;
case 3:
uv *= 2;
vec3 p = vec3((uv - 0.5) * 2.0, floor(mod(0, 8.0)));
vec3 woodColor = pow(matWood(p, 
    vec3(0.15, 0.05, 0.03),  // Base color: Molto scuro
    vec3(0.30, 0.10, 0.05),  // Mid-tone color: Più scuro del colore principale
    vec3(0.40, 0.15, 0.08)   // Highlight color: Riflessi scuri
), vec3(.4545));
 color = woodColor;
break;
    }

    
      
    } else if (uMaterialID == 3) {
    switch(uMaterialBoard){
    case 1: 
color = vec3(0.40, 0.26, 0.13);
break;
case 2:
vec3 p = vec3((uv - 0.5) * 2.0, floor(mod(0, 8.0)));
   vec3 baseColor = vec3(0.10, 0.05, 0.02);      // Fondo del legno scuro
vec3 midColor = vec3(0.30, 0.15, 0.07);       // Tono intermedio
vec3 highlightColor = vec3(0.45, 0.25, 0.12); // Venature chiare
vec3 woodColor = pow(matWood(p, baseColor, midColor, highlightColor), vec3(.4545));
color = woodColor;
break;
case 3: 
uv *=.25;
 p = vec3((uv - 0.5) * 2.0, floor(mod(0, 8.0)));
 woodColor = pow(
    matWood(
        p,
        vec3(0.02, 0.05, 0.10),  // Fondo del legno blu scuro
        vec3(0.07, 0.15, 0.30),  // Tono intermedio
        vec3(0.12, 0.25, 0.45)   // Venature chiare
    ),
    vec3(0.4545)
);
color = woodColor;
break;
}

    }
else if (uMaterialID == 4) {
switch(uMaterialWhiteSquares){
case 1:
color = vec3(0.75);
break;
case 2: 
uv *=.25;
vec3 p = vec3((uv - 0.5) * 2.0, floor(mod(0, 8.0)));
   vec3 woodColor = pow(matWood(p, 
    vec3(0.9, 0.9, 0.85),  // Base color
    vec3(0.8, 0.8, 0.75),  // Mid-tone color
    vec3(1.0, 1.0, 0.95)   // Highlight color
), vec3(.4545));
 color = woodColor;
break;}

    }
else if (uMaterialID == 5) {
switch(uMaterialBlackSquares){
case 1: 
color = vec3(0.25);
break; 
case 2: 
uv *=.25;
vec3 p = vec3((uv - 0.5) * 2.0, floor(mod(0, 8.0)));
   vec3 baseColor = vec3(0.10, 0.05, 0.02);      // Fondo del legno scuro
vec3 midColor = vec3(0.30, 0.15, 0.07);       // Tono intermedio
vec3 highlightColor = vec3(0.45, 0.25, 0.12); // Venature chiare
vec3 woodColor = pow(matWood(p, baseColor, midColor, highlightColor), vec3(.4545));
color = woodColor;
break;
case 3: 
uv *=.25;
 p = vec3((uv - 0.5) * 2.0, floor(mod(0, 8.0)));
 woodColor = pow(
    matWood(
        p,
        vec3(0.02, 0.05, 0.10),  // Fondo del legno blu scuro
        vec3(0.07, 0.15, 0.30),  // Tono intermedio
        vec3(0.12, 0.25, 0.45)   // Venature chiare
    ),
    vec3(0.4545)
);
color = woodColor;
break;

}

    }
 else {
        // Default fallback
        color = vec3(0,1,0);
    }
// Add basic lighting (Phong reflection model)
// Ambient
  vec3 ambient = vec3(0.5, 0.5, 0.5);

// Diffuse
vec3 normal = normalize(Normal);
vec3 lightSource = vec3(1.0, 1.0, 1.0); // coord - (1, 0, 0)
float diffuseStrength = max(0.0, dot(lightSource, normal));
vec3 diffuse = diffuseStrength * color;

//Specular 
vec3 cameraSource = vec3(0.0, 0.0, 1.0);
  vec3 viewSource = normalize(cameraDir);
  vec3 reflectSource = normalize(reflect(-lightSource, normal));
  float specularStrength = max(0.0, dot(viewSource, reflectSource));
  specularStrength = pow(specularStrength, 256.0);
  vec3 specular = specularStrength * color;

 vec3 lighting = ambient * 0.5 + diffuse * 0.5 + specular * 0.5;

vec3 finalColor = ambient + diffuse ;
FragColor = vec4(color*lighting, 1.0);
}
).";



GLuint createShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    // Check compilation
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "ERROR Shader compilation: " << infoLog << std::endl;
    }
    return shader;
}

GLuint createProgram(const char* vs, const char* fs)
{
    GLuint vshader = createShader(GL_VERTEX_SHADER, vs);
    GLuint fshader = createShader(GL_FRAGMENT_SHADER, fs);
    GLuint program = glCreateProgram();
    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glLinkProgram(program);

    // check linking
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "ERROR Program linking: " << infoLog << std::endl;
    }
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    return program;
}


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    gWindowWidth = width;
    gWindowHeight = height;
    glViewport(0, 0, width, height);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            rightClickPressed = true;
            glfwGetCursorPos(window, &lastX, &lastY);
        }
        else if (action == GLFW_RELEASE) {
            rightClickPressed = false;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (!rightClickPressed) return; // only rotate if right click is down

    float xoffset = (float)xpos - (float)lastX;
    float yoffset = (float)ypos - (float)lastY;
    lastX = xpos;
    lastY = ypos;

    // Adjust sensitivity
    float sensitivity = 0.3f;
    xoffset *= -sensitivity; // invert horizontal movement for convenience
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // clamp pitch
    if (pitch > 89.9f)  pitch = 89.9f;
    if (pitch < -89.9f) pitch = -89.9f;
}

// Scroll for zoom in/out
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    // Increase or decrease the orbit radius
    radius -= (float)yoffset * zoomSpeed;
    if (radius < 1.0f)  radius = 1.0f;
    if (radius > 100.f) radius = 100.f;
}

void processInput(GLFWwindow* window)
{
    // escape
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// ---------------------------------------------------
bool initWindowAndGL()
{
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    gWindow = glfwCreateWindow(gWindowWidth, gWindowHeight, "PGRe Project", nullptr, nullptr);
    if (!gWindow) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(gWindow);
    glfwSetFramebufferSizeCallback(gWindow, framebuffer_size_callback);

    // set up mouse
    glfwSetMouseButtonCallback(gWindow, mouse_button_callback);
    glfwSetCursorPosCallback(gWindow, cursor_position_callback);
    glfwSetScrollCallback(gWindow, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return false;
    }
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, gWindowWidth, gWindowHeight);

    return true;
}

// ---------------------------------------------------
// MAIN
int main()
{
    // 1) Initialize
    if (!initWindowAndGL()) return -1;
    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForOpenGL(gWindow, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImGui::StyleColorsDark();

   
    GLuint program = createProgram(vsSrc, fsSrc2);
     
    
    Model myChessboard("chessboard1.fbx");
   

    //Setup projection
    gProjection = glm::perspective(glm::radians(45.0f),
        (float)gWindowWidth / (float)gWindowHeight,
        0.1f, 100.0f);

    // 5) Main loop
    while (!glfwWindowShouldClose(gWindow)) {
        // compute deltaTime
        float currentTime = (float)glfwGetTime();
        deltaTime = currentTime - lastFrame;
        lastFrame = currentTime;


        processInput(gWindow);
      
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

     

        // clear
        glClearColor(0.5f, 0.6f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Recompute cameraPos from yaw, pitch, radius, plus cameraOffset
        float radYaw = glm::radians(yaw);
        float radPitch = glm::radians(pitch);

        float x = radius * cos(radPitch) * sin(radYaw);
        float y = radius * sin(radPitch);
        float z = radius * cos(radPitch) * cos(radYaw);

        glm::vec3 position = orbitCenter + glm::vec3(x, y, z) + cameraOffset;
        glm::vec3 front = glm::normalize(orbitCenter + cameraOffset - position);
        glm::vec3 cameraDir = glm::normalize(position - orbitCenter);

        // build the view matrix
        gView = glm::lookAt(position, position + front, glm::vec3(0.f, 1.f, 0.f));

        // use shader
        glUseProgram(program);

        // set uniform view / proj
        GLint viewLoc = glGetUniformLocation(program, "view");
        GLint projLoc = glGetUniformLocation(program, "projection");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(gView));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(gProjection));
        GLint cameraDirLoc = glGetUniformLocation(program, "cameraDir");
        glUniform3f(cameraDirLoc, cameraDir.x, cameraDir.y, cameraDir.z);

        // chessboard rotation
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, glm::radians(-90.f), glm::vec3(1, 0, 0));
        model = glm::translate(model, glm::vec3(0.f, -1.f, 0.f));

        GLint modelLoc = glGetUniformLocation(program, "model");
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

        myChessboard.Draw(program);

        // 2. User interface
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));               
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));                 
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 1)); 
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1));  
        ImVec2 viewportSize = ImGui::GetIO().DisplaySize;
        ImVec2 textPosition = ImVec2(viewportSize.x - 400, viewportSize.y - 50); // Posizionamento
        ImGui::SetNextWindowPos(textPosition, ImGuiCond_Always, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        ImGui::SetWindowSize(ImVec2(400, 50)); // Dimensione più grande per contenere il testo
        ImGui::Text("Hold Right Click and move the mouse to rotate");
        ImGui::Text("Scroll Wheel to Zoom In/Out");
        ImGui::End();
        ImGui::Begin("Material Selector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);

        // Uniform spacing
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

        // Section: White Pieces
        ImGui::Text("White Pieces");
        ImGui::SameLine(150);
        if (ImGui::Button("<##White")) {
            materialWhite--;
            if (materialWhite < 1) materialWhite = maxMaterialsPieces;
        }
        ImGui::SameLine();
        ImGui::Text("Material %d", materialWhite);
        ImGui::SameLine();
        if (ImGui::Button(">##White")) {
            materialWhite++;
            if (materialWhite > maxMaterialsPieces) materialWhite = 1;
        }

        // Section: Black Pieces
        ImGui::Text("Black Pieces");
        ImGui::SameLine(150);
        if (ImGui::Button("<##Black")) {
            materialBlack--;
            if (materialBlack < 1) materialBlack = maxMaterialsPieces;
        }
        ImGui::SameLine();
        ImGui::Text("Material %d", materialBlack);
        ImGui::SameLine();
        if (ImGui::Button(">##Black")) {
            materialBlack++;
            if (materialBlack > maxMaterialsPieces) materialBlack = 1;
        }

        // Section: Board Base
        ImGui::Text("Board Base");
        ImGui::SameLine(150);
        if (ImGui::Button("<##Base")) {
            materialBase--;
            if (materialBase < 1) materialBase = maxMaterialsPieces;
        }
        ImGui::SameLine();
        ImGui::Text("Material %d", materialBase);
        ImGui::SameLine();
        if (ImGui::Button(">##Base")) {
            materialBase++;
            if (materialBase > maxMaterialsPieces) materialBase = 1;
        }

        // Section: White Squares
        ImGui::Text("White Squares");
        ImGui::SameLine(150);
        if (ImGui::Button("<##WS")) {
            materialWhiteSquares--;
            if (materialWhiteSquares < 1) materialWhiteSquares = maxMaterials;
        }
        ImGui::SameLine();
        ImGui::Text("Material %d", materialWhiteSquares);
        ImGui::SameLine();
        if (ImGui::Button(">##WS")) {
            materialWhiteSquares++;
            if (materialWhiteSquares > maxMaterials) materialWhiteSquares = 1;
        }

        // Section: Black Squares
        ImGui::Text("Black Squares");
        ImGui::SameLine(150);
        if (ImGui::Button("<##BS")) {
            materialBlackSquares--;
            if (materialBlackSquares < 1) materialBlackSquares = maxMaterialsPieces;
        }
        ImGui::SameLine();
        ImGui::Text("Material %d", materialBlackSquares);
        ImGui::SameLine();
        if (ImGui::Button(">##BS")) {
            materialBlackSquares++;
            if (materialBlackSquares > maxMaterialsPieces) materialBlackSquares = 1;
        }

        ImGui::PopStyleVar(); // Restore spacing
        ImGui::End();

        ImGui::PopStyleColor(4); // Restore colors
        

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        GLint blackMaterialLoc = glGetUniformLocation(program, "uMaterialBlack");
        GLint whiteMaterialLoc = glGetUniformLocation(program, "uMaterialWhite");
        GLint blackSqMaterialLoc = glGetUniformLocation(program, "uMaterialBlackSquares");
        GLint whiteSqMaterialLoc = glGetUniformLocation(program, "uMaterialWhiteSquares");
        GLint boardMaterialLoc = glGetUniformLocation(program, "uMaterialBoard");
        glUniform1i(blackMaterialLoc, materialBlack);
        glUniform1i(whiteMaterialLoc, materialWhite);
        glUniform1i(blackSqMaterialLoc, materialBlackSquares);
        glUniform1i(whiteSqMaterialLoc, materialWhiteSquares);
        glUniform1i(boardMaterialLoc, materialBase);
        // swap
        glfwSwapBuffers(gWindow);
        glfwPollEvents();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}
