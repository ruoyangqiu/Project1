// ParticlesApp.cpp - demonstrate ballistic particles bouncing off cylinders

#include <glad.h>
#include <GLFW/glfw3.h>
#include <time.h>
#include "CameraArcball.h"
#include "Draw.h"
#include "GLXtras.h"

float		PI = 3.141592f;
float		gravity = 1;
float		ground = 0;

// display and interaction
CameraAB	camera(100, 100, 800, 800, vec3(20, 90, 0), vec3(0, 0, -5));
vec3		light(1, 1, -.6f);
Mover		mover;
void	   *picked = NULL;

// Miscellaneous

float Lerp(float a, float b, float alpha) {
	return a+alpha*(b-a);
}

float Blend(float x) {
    // cubic blend function f: f(0)=1, f(1)=0, f'(0)=f'(1)=0
    x = abs(x);
    float x2 = x*x, x4 = x2*x2;
    return x < FLT_MIN? 1 : x > 1? 0 : (-4.f/9.f)*x2*x4+(17.f/9.f)*x4+(-22.f/9.f)*x2+1;
};

float Random() {
    // random number between 0 and 1
	return (float) (rand()%1000)/1000.f;
}

float Random(float a, float b) {
    // random number between a and b
	return Lerp(a, b, Random());
}

// Cylinders

struct Vertex {
	vec3 point, normal;
	Vertex() { }
	Vertex(vec3 &p, vec3 &n) : point(p), normal(n) { }
};

const char *cylVertexShader = R"(
	#version 130
	in vec3 point;
	in vec3 normal;
	out vec3 vPoint;
	out vec3 vNormal;
    uniform mat4 view;
	uniform mat4 persp;
	void main() {
		vPoint = (view*vec4(point, 1)).xyz;
		gl_Position = persp*vec4(vPoint, 1);
		vNormal = (view*vec4(normal, 0)).xyz;
	}
)";

const char *cylPixelShader = R"(
    #version 130
	in vec3 vPoint;
	in vec3 vNormal;
	out vec4 pColor;
	uniform vec4 color = vec4(1,1,1,1);
	uniform vec3 light;
    void main() {
		vec3 N = normalize(vNormal);          // surface normal
        vec3 L = normalize(light-vPoint);     // light vector
        vec3 E = normalize(vPoint);           // eye vector
        vec3 R = reflect(L, N);			      // highlight vector
        float d = abs(dot(N, L));             // two-sided diffuse
        float s = abs(dot(R, E));             // two-sided specular
		float intensity = clamp(d+pow(s, 50), 0, 1);
		pColor = vec4(intensity*color.rgb, color.a);
	}
)";

GLuint cylBufferId = 0;	// buffer for cylinder vertices
GLuint cylShaderId = 0;	// shader program for cylinders

void MakeCylinderVertexBuffer() {
	// vertex buffer for canonical cylinder (base at origin in xz plane, height to y=1, radius=1)
	// individual cylinders drawn using mat4 computed from position, height & radius
	int vCount = 0;
	// set 24 circumferential pts, 4 triangles each: 4X24X3 = 288 vertices
	Vertex verts[288];
	vec3 pBot = vec3(0, 0, 0), pTop = vec3(0, 1, 0), nBot(0, -1, 0), nTop(0, 1, 0);
	for (int i1 = 0; i1 < 24; i1++) {
		int i2 = (i1+1)%24;
		float a1 = 2*PI*(float)i1/24, x1 = cos(a1), z1 = sin(a1);
		float a2 = 2*PI*(float)i2/24, x2 = cos(a2), z2 = sin(a2);
		vec3 n1(x1, 0, z1), p1Bot(pBot+n1), p1Top(pTop+n1);
		vec3 n2(x2, 0, z2), p2Bot(pBot+n2), p2Top(pTop+n2);
		// bottom wedge triangle
		verts[vCount++] = Vertex(pBot, nBot);
		verts[vCount++] = Vertex(p1Bot, nBot);
		verts[vCount++] = Vertex(p2Bot, nBot);	
		// top wedge triangle
		verts[vCount++] = Vertex(pTop, nTop);
		verts[vCount++] = Vertex(p1Top, nTop);
		verts[vCount++] = Vertex(p2Top, nTop);	
		// side top triangle
		verts[vCount++] = Vertex(p1Bot, n1);
		verts[vCount++] = Vertex(p1Top, n1);
		verts[vCount++] = Vertex(p2Top, n2);	
		// side bottom triangle
		verts[vCount++] = Vertex(p2Top, n2);
		verts[vCount++] = Vertex(p2Bot, n2);
		verts[vCount++] = Vertex(p1Bot, n1);
	}
	glGenBuffers(1, &cylBufferId);
	glBindBuffer(GL_ARRAY_BUFFER, cylBufferId);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
}

class Cylinder {
public:
    float height = 0, radius = 0;
	vec3 color, location;
	Cylinder() { }
	Cylinder (float h, float r, vec3 c, vec3 l) : height(h), radius(r), color(c), location(l) { }
	bool Inside(vec3 p) {
		if (p.y > ground+height)
			return false;
		float dx = p.x-location.x, dz = p.z-location.z;
		return dx*dx+dz*dz < radius*radius;
	}
    void Draw() {
		glBindBuffer(GL_ARRAY_BUFFER, cylBufferId);
		VertexAttribPointer(cylShaderId, "point", 3, sizeof(Vertex), (void *) 0);
		VertexAttribPointer(cylShaderId, "normal", 3, sizeof(Vertex), (void *) sizeof(vec3));
		SetUniform(cylShaderId, "color", vec4(color.x, color.y, color.z, 1));
		mat4 m = camera.modelview*Translate(location)*Scale(radius, height, radius);
		SetUniform(cylShaderId, "view", m);
		glDrawArrays(GL_TRIANGLES, 0, 288);
    }
};

Cylinder cylinders[] = {
	Cylinder(.5f, .25f, vec3(1, .7f, 0), vec3(-.3f, ground, .6f)),
	Cylinder(.35f, .5f, vec3(0, 0, .7f), vec3(.3f, ground, -.2f)),
	Cylinder(.25f, .35f, vec3(0, .7f, 0), vec3(.2f, ground, -.7f))
};

// Particle

class Particle {
public:
    int		level;		// first generation is level 0
	bool	grounded;   // has particle returned to earth?
	clock_t	birth;      // when instantiated
	float	lifetime;   // duration of activity
	float	speed;      // speed of motion per unit time
	float	size;       // in pixels
	float	emitRate;   // sub-particle emission rate if grounded
	clock_t prevEmit;   // when last spawned another particle
	vec3	position;	// current location in 3D
	vec3	velocity;	// current linear 3D direction
	vec3	color;		// r, g, b
    void Init(int l, float lt, float s, float sz, float er) {
		grounded = false;
		birth = clock();
		level = l;
		lifetime = lt;
		speed = s;
		size = sz;
		emitRate = er;
	}
    void Move(float deltaTime) {
		// update vertical component of velocity
		velocity[1] -= deltaTime*gravity;
		normalize(velocity);
		// update position
		position += speed*deltaTime*velocity;
        // bounce against cylinders
		int nCylinders = sizeof(cylinders)/sizeof(Cylinder);
        for (int c = 0; c < nCylinders; c++) {
            Cylinder &cyl = cylinders[c];
            if (cyl.Inside(position) && velocity[1] < 0) {
                position[1] = cyl.height;
                velocity[1] = -.5f*velocity[1];
                break;
            }
        }
    }
	void Update(float deltaTime) {
		// move ungrounded particle
        if (!grounded) {
            Move(deltaTime);
			// test for grounded
			if (position[1] <= ground) {
				position[1] = ground;
				grounded = true;
                prevEmit = clock();
			}
		}
	}
};

// Emitter creates new particles, recycles old particles

#define MAX_PARTICLES 5000

class Emitter {
public:
	clock_t  prevTime;                 // needed to compute delta time
	clock_t  nextEmitTime;             // to control particle emissions
	Particle minParticle;              // minimum values for position, size, etc
    Particle maxParticle;              // maximum values
	Particle particles[MAX_PARTICLES]; // array of particles
	int      nparticles;               // # elements in array
	Emitter() {
		prevTime = clock();
		nparticles = 0;
		nextEmitTime = 0;
		srand((int) time(NULL));
		minParticle.Init(-1, .15f, .1f, 5, 15); // level, lifetime, speed, size, emitRate
		maxParticle.Init(-1, 7.f, .4f, 9, 50);
		minParticle.position = maxParticle.position = vec3(0, 1, 0);
		minParticle.color = vec3(0, 0, 0);
		maxParticle.color = vec3(1, 1, 1);
	}
	void CreateParticle(int level = 0, vec3 *pos = NULL, vec3 *col = NULL) {
		// create new particle randomly between minParticle and maxParticle
        if (nparticles < MAX_PARTICLES) {
		    Particle &p = particles[nparticles++];
            float b = Blend(level/10.f);
            float lifetime = Lerp(minParticle.lifetime, maxParticle.lifetime, b*Random());
            float speed =    Lerp(minParticle.speed,    maxParticle.speed,    b*Random());
            float size =     Lerp(minParticle.size,     maxParticle.size,     b*Random());
            float emitRate = Lerp(minParticle.emitRate, maxParticle.emitRate, b*Random());
            p.Init(level, lifetime, speed, size, emitRate);
            // set position, using argument if given
            if (pos)
                p.position = *pos;
            else
                // use min/max particle position
                for (int i = 0; i < 3; i++)
                    p.position[i] = Random(minParticle.position[i], maxParticle.position[i]);
            // set color, using argument if given
            if (col)
                p.color = *col;
            else
                for (int k = 0; k < 3; k++)
                    p.color[k] = Random(minParticle.color[k], maxParticle.color[k]);
            // set velocity
            float azimuth = Random(0., 2.f*PI);
            float elevation = Random(0., PI/2.f);
            float cosElevation = cos(elevation);
            p.velocity[1] = sin(elevation);
            p.velocity[2] = cosElevation*sin(azimuth);
            p.velocity[0] = cosElevation*cos(azimuth);
        }
	}
	void Draw() {
		UseDrawShader(camera.fullview);
		for (int i = 0; i < nparticles; i++) {
			Particle &p = particles[i];
			Disk(p.position, p.size, p.color);
		}
	}
    void Update() {
        // need delta time to regulate speed
		clock_t now = clock();
		float dt = (float) (now-prevTime)/CLOCKS_PER_SEC;
		prevTime = now;
		// delete expired particles
		for (int i = 0; i < nparticles;) {
			Particle &p = particles[i];
            if (now > p.birth+p.lifetime*CLOCKS_PER_SEC) {
                // delete particle
		        if (i < nparticles-1)
		            particles[i] = particles[nparticles-1];
	            nparticles--;
            }
			else
				i++;
		}
		// update ungrounded particles
		for (int k = 0; k < nparticles; k++) {
			Particle &p = particles[k];
            if (p.grounded) {
				float dt = (float) (now-p.prevEmit)/CLOCKS_PER_SEC;
				if (dt > 1./p.emitRate) {
				    // spawn new particle
                    CreateParticle(p.level+1, &p.position, &p.color);
					p.prevEmit = now;
				}
			}
			else
                p.Update(dt);
		}
		// possibly emit new particle
        if (now > nextEmitTime) {
			CreateParticle();
			float randomBoundedEmitRate = Random(minParticle.emitRate, maxParticle.emitRate);
			nextEmitTime = now+(clock_t)((float)CLOCKS_PER_SEC/randomBoundedEmitRate);
	  	}
	}
} emitter;

// Display

void Display() {
	// set screen grey, enable transparency, use z-buffer
    glClearColor(.5f, .5f, .5f, 1);
    glClear(GL_COLOR_BUFFER_BIT |  GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	// draw cylinders
	glUseProgram(cylShaderId);
	SetUniform(cylShaderId, "persp", camera.persp);
	vec4 hLight = camera.modelview*vec4(light, 1);
	SetUniform(cylShaderId, "light", vec3(hLight.x, hLight.y, hLight.z));
	int nCylinders = sizeof(cylinders)/sizeof(Cylinder);
	for (int i = 0; i < nCylinders; i++)
        cylinders[i].Draw();
	// draw particles
	emitter.Draw();
	// draw light source
	UseDrawShader(camera.fullview);
	Disk(light, 12, vec3(1, 0, 0));
    // finish
    glFlush();
}

// Interactive Camera / Light

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
    y = WindowHeight(w)-y; // invert y for upward-increasing screen space
    picked = NULL;
    if (action == GLFW_PRESS && butn == GLFW_MOUSE_BUTTON_LEFT) {
        if (MouseOver((float) x, (float) y, light, camera.fullview)) {
            picked = &light;
            mover.Down(&light, (int) x, (int) y, camera.modelview, camera.persp);
        }
        else {
            picked = &camera;
            camera.MouseDown((int) x, (int) y);
        }
    }
    if (action == GLFW_RELEASE)
        camera.MouseUp();
}

void MouseMove(GLFWwindow *w, double x, double y) {
    if (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        y = WindowHeight(w)-y;
        if (picked == &light)
            mover.Drag((int) x, (int) y, camera.modelview, camera.persp);
        if (picked == &camera)
            camera.MouseDrag((int) x, (int) y, Shift(w));
    }
}

void MouseWheel(GLFWwindow *w, double xoffset, double direction) {
    camera.MouseWheel(direction > 0);
}

// Application

void Resize(GLFWwindow *w, int width, int height) {
    glViewport(0, 0, width, height);
}

int main(int ac, char **av) {
    // init app window and GL context
    glfwInit();
    GLFWwindow *w = glfwCreateWindow(800, 800, "Particles", NULL, NULL);
    glfwSetWindowPos(w, 100, 100);
    glfwMakeContextCurrent(w);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
	// set cylinder vertex buffer and shader
	MakeCylinderVertexBuffer();
	cylShaderId = LinkProgramViaCode(&cylVertexShader, &cylPixelShader);
	if (!cylShaderId) {
		printf("can't link shader program\n");
		getchar();
		return 0;
	}
	// callbacks
    glfwSetCursorPosCallback(w, MouseMove);
    glfwSetMouseButtonCallback(w, MouseButton);
    glfwSetScrollCallback(w, MouseWheel);
    glfwSetWindowSizeCallback(w, Resize);
    // event loop
    glfwSwapInterval(1);
    while (!glfwWindowShouldClose(w)) {
		emitter.Update();
        Display();
        glfwSwapBuffers(w);
        glfwPollEvents();
    }
 	// unbind vertex buffer, free GPU memory
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &cylBufferId);
    glfwDestroyWindow(w);
    glfwTerminate();
	return 0;
}
