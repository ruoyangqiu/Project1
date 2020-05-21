// 3D-Slide.cpp: display quad as ground and 3D OBJ model with texture

#include <glad.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <time.h>
#include "CameraArcball.h"
#include "Draw.h"
#include "GLXtras.h"
#include "Mesh.h"
#include "Misc.h"
#include "Widgets.h"
#include "Draw.h"
#include "Quaternion.h"


// display
GLuint      shaderProgram = 0;
int         winW = 600, winH = 600;
CameraAB    camera(0, 0, winW, winH, vec3(0,0,0), vec3(0,0,-5));
int			gTextureUnit = 0;

// interaction
vec3        light(-.2f, .4f, .3f);
Framer      framer;     // position/orient individual mesh
Mover       mover;      // position light
void       *picked = &camera;

// Mesh Class

class Mesh {
public:
	Mesh() { };
    // vertices and triangles
    vector<vec3> points;
    vector<vec3> normals;
    vector<vec2> uvs;
    vector<int3> triangles;
    // object to world space transformation
    mat4 xform;
    // GPU vertex buffer and texture
    GLuint vBufferId = 0, textureId = 0, textureUnit = 0;
    // operations
    void Buffer();
    void Draw();
    bool Read(const char *fileame, const char *textureName);
};

Mesh mesh;

const char *catObj = "C:/Users/jules/SeattleUniversity/Web/Models/Cat.obj";
const char *catTex = "C:/Users/jules/SeattleUniversity/Web/Models/Cat.tga";

// Shaders

const char *vertexShader = R"(
    #version 130
    in vec3 point;
    in vec3 normal;
    in vec2 uv;
    out vec3 vPoint;
    out vec3 vNormal;
    out vec2 vUv;
    uniform mat4 modelview;
    uniform mat4 persp;
    void main() {
        vPoint = (modelview*vec4(point, 1)).xyz;
        vNormal = (modelview*vec4(normal, 0)).xyz;
        gl_Position = persp*vec4(vPoint, 1);
        vUv = uv;
    }
)";

const char *pixelShader = R"(
    #version 130
    in vec3 vPoint;
    in vec3 vNormal;
    in vec2 vUv;
    out vec4 pColor;
    uniform vec3 light;
    uniform sampler2D textureImage;
    void main() {
        vec3 N = normalize(vNormal);       // surface normal
        vec3 L = normalize(light-vPoint);  // light vector
        vec3 E = normalize(vPoint);        // eye vector
        vec3 R = reflect(L, N);            // highlight vector
        float d = abs(dot(N, L));          // two-sided diffuse
        float s = abs(dot(R, E));          // two-sided specular
        float intensity = clamp(d+pow(s, 50), 0, 1);
        vec3 color = texture(textureImage, vUv).rgb;
        pColor = vec4(intensity*color, 1);
    }
)";

// Mesh

void Mesh::Buffer() {
	int nPts = points.size(), nNrms = normals.size(), nUvs = uvs.size();
	if (!nPts || nPts != nNrms || nPts != nUvs) {
		printf("mesh missing points, normals, or uvs\n");
		return;
	}
    // create a vertex buffer for the mesh
    glGenBuffers(1, &vBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, vBufferId);
    // allocate GPU memory for vertex locations and colors
    int sizePoints = points.size()*sizeof(vec3);
    int sizeNormals = normals.size()*sizeof(vec3);
    int sizeUvs = uvs.size()*sizeof(vec2);
    int bufferSize = sizePoints+sizeUvs+sizeNormals;
    glBufferData(GL_ARRAY_BUFFER, bufferSize, NULL, GL_STATIC_DRAW);
    // load data to buffer
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizePoints, &points[0]);
    glBufferSubData(GL_ARRAY_BUFFER, sizePoints, sizeNormals, &normals[0]);
    glBufferSubData(GL_ARRAY_BUFFER, sizePoints+sizeNormals, sizeUvs, &uvs[0]);
}

void Mesh::Draw() {
	int nPts = points.size(), nNrms = normals.size(), nUvs = uvs.size(), nTris = triangles.size();
	if (!nPts || !nNrms || !nUvs || !nTris)
		return;
    // use vertex buffer for this mesh
    glBindBuffer(GL_ARRAY_BUFFER, vBufferId);
    // connect shader inputs to GPU buffer
    int sizePoints = nPts*sizeof(vec3);
    int sizeNormals = nNrms*sizeof(vec3);
    // vertex feeder
    VertexAttribPointer(shaderProgram, "point", 3, 0, (void *) 0);
    VertexAttribPointer(shaderProgram, "normal", 3, 0, (void *) sizePoints);
    VertexAttribPointer(shaderProgram, "uv", 2, 0, (void *) (sizePoints+sizeNormals));
    // set custom transform (xform = mesh transforms X view transform)
    glActiveTexture(GL_TEXTURE1+textureUnit);    // active texture corresponds with textureUnit
    glBindTexture(GL_TEXTURE_2D, textureId);
    SetUniform(shaderProgram, "textureImage", (int) textureId);
    SetUniform(shaderProgram, "modelview", camera.modelview*xform);
    SetUniform(shaderProgram, "persp", camera.persp);
	glDrawElements(GL_TRIANGLES, 3*nTris, GL_UNSIGNED_INT, &triangles[0]);
}

bool Mesh::Read(const char *meshName, const char *textureName) {
     // read in object file (with normals, uvs) and texture map, initialize matrix, build vertex buffer
    if (!ReadAsciiObj(meshName, points, triangles, &normals, &uvs)) {
        printf("can't read %s\n", meshName);
        return false;
    }
    Normalize(points, .8f);
    Buffer();
	textureUnit = gTextureUnit++;
    textureId = LoadTexture(textureName, textureUnit);
    framer.Set(&xform, 100, camera.persp*camera.modelview);
    return true;
}

class Ground {
public:
	Ground() { };
    // GPU vertex buffer and texture
    GLuint vBufferId = 0, textureId = 0, textureUnit = 0;
	size_t sizePts = 0, sizeNrms = 0, sizeUvs = 0;
    // operations
    void Buffer() {
		float size = 5, ht = -.55f;
		vec3 points[] = { vec3(-size, ht, -size), vec3(size, ht, -size), vec3(size, ht, size), vec3(-size, ht, size) };
		vec3 normals[] = { vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1) };
		vec2 uvs[] = { vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1) };
		sizePts = sizeof(points);
		sizeNrms = sizeof(normals);
		sizeUvs = sizeof(uvs);
		// allocate and download vertices
		glGenBuffers(1, &vBufferId);
		glBindBuffer(GL_ARRAY_BUFFER, vBufferId);
		glBufferData(GL_ARRAY_BUFFER, sizePts+sizeNrms+sizeUvs, NULL, GL_STATIC_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizePts, points);
		glBufferSubData(GL_ARRAY_BUFFER, sizePts, sizeNrms, normals);
		glBufferSubData(GL_ARRAY_BUFFER, sizePts+sizeNrms, sizeUvs, uvs);
		textureUnit = gTextureUnit++;
		textureId = LoadTexture("C:/Users/jules/SeattleUniversity/Exe/Lily.tga", textureUnit);
	}
    void Draw() {
		glBindBuffer(GL_ARRAY_BUFFER, vBufferId);
		// render four vertices as a quad
		VertexAttribPointer(shaderProgram, "point", 3, 0, (void *) 0);
		VertexAttribPointer(shaderProgram, "normal", 3, 0, (void *) sizePts);
		VertexAttribPointer(shaderProgram, "uv", 2, 0, (void *) (sizePts+sizeNrms));
		glActiveTexture(GL_TEXTURE1+textureUnit);
		glBindTexture(GL_TEXTURE_2D, textureId);
		SetUniform(shaderProgram, "textureImage", (int) textureId);
		SetUniform(shaderProgram, "modelview", camera.modelview);
		SetUniform(shaderProgram, "persp", camera.persp);
		glDrawArrays(GL_QUADS, 0, 4);
	}
};

Ground ground;

// Display

time_t mouseMoved;

void Display() {
    // clear screen, depth test, blend
    glClearColor(.5f, .5f, .5f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glUseProgram(shaderProgram);
    // update light
    vec4 xlight = camera.modelview*vec4(light, 1);
    SetUniform3(shaderProgram, "light", (float *) &xlight.x);
    // display objects
    mesh.Draw();
	ground.Draw();
    // lights and frames
    if ((clock()-mouseMoved)/CLOCKS_PER_SEC < 1.f) {
        glDisable(GL_DEPTH_TEST);
        UseDrawShader(camera.fullview);
        Disk(light, 9, vec3(1,1,0));
        mat4 &f = mesh.xform;
        vec3 base(f[0][3], f[1][3], f[2][3]);
        Disk(base, 9, vec3(1,1,1));
        if (picked == &framer)
            framer.Draw(camera.fullview);
        if (picked == &camera)
            camera.arcball.Draw();
    }
    glFlush();
}

// Mouse

int WindowHeight(GLFWwindow *w) {
    int width, height;
    glfwGetWindowSize(w, &width, &height);
    return height;
}

bool Shift(GLFWwindow *w) {
    return glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
           glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

void MouseButton(GLFWwindow *w, int butn, int action, int mods) {
    double x, y;
    glfwGetCursorPos(w, &x, &y);
	y = WindowHeight(w)-y;
	int ix = (int) x, iy = (int) y;
    if (action == GLFW_PRESS && butn == GLFW_MOUSE_BUTTON_LEFT) {
        void *newPicked = NULL;
        if (MouseOver(x, y, light, camera.fullview)) {
            newPicked = &mover;
            mover.Down(&light, ix, iy, camera.modelview, camera.persp);
        }
        if (!newPicked) {
            // test for mesh base hit
            vec3 base(mesh.xform[0][3], mesh.xform[1][3], mesh.xform[2][3]);
            if (MouseOver(x, y, base, camera.fullview)) {
                newPicked = &framer;
                framer.Set(&mesh.xform, 100, camera.fullview);
                framer.Down(ix, iy, camera.modelview, camera.persp);
            }
            }
        if (!newPicked && picked == &framer && framer.Hit(ix, iy)) {
            framer.Down(ix, iy, camera.modelview, camera.persp);
            newPicked = &framer;
        }
        picked = newPicked;
        if (!picked) {
            picked = &camera;
            camera.MouseDown(x, y);
        }
    }
    if (action == GLFW_RELEASE) {
        if (picked == &camera)
            camera.MouseUp();
        if (picked == &framer)
            framer.Up();
    }
}

void MouseMove(GLFWwindow *w, double x, double y) {
    mouseMoved = clock();
    if (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) { // drag
        y = WindowHeight(w)-y;
		int ix = (int) x, iy = (int) y;
        if (picked == &mover)
            mover.Drag(ix, iy, camera.modelview, camera.persp);
        if  (picked == &framer)
            framer.Drag(ix, iy, camera.modelview, camera.persp);
        if (picked == &camera)
            camera.MouseDrag(x, y, Shift(w));
    }
}

void MouseWheel(GLFWwindow *w, double xoffset, double direction) {
    if (picked == &framer)
        framer.Wheel(direction > 0, Shift(w));
    if (picked == &camera)
        camera.MouseWheel(direction > 0, Shift(w));
}

// Application

void Resize(GLFWwindow *w, int width, int height) {
    glViewport(0, 0, winW = width, winH = height);
    camera.Resize(width, height);
}

int main(int ac, char **av) {
    // init app window and GL context
    glfwInit();
    GLFWwindow *w = glfwCreateWindow(winW, winH, "MultiMesh", NULL, NULL);
    glfwSetWindowPos(w, 100, 100);
    glfwMakeContextCurrent(w);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    // build shader program, read scene file
    shaderProgram = LinkProgramViaCode(&vertexShader, &pixelShader);
	mesh.Read(catObj, catTex);
	ground.Buffer();
    Resize(w, winW, winH); // initialize camera.arcball.fixedBase
    // callbacks
    glfwSetCursorPosCallback(w, MouseMove);
    glfwSetMouseButtonCallback(w, MouseButton);
    glfwSetScrollCallback(w, MouseWheel);
    glfwSetWindowSizeCallback(w, Resize);
    // event loop
    glfwSwapInterval(1);
    while (!glfwWindowShouldClose(w)) {
        Display();
        glfwSwapBuffers(w);
        glfwPollEvents();
    }
    // unbind vertex buffer, free GPU memory
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &mesh.vBufferId);
    glfwDestroyWindow(w);
    glfwTerminate();
}
