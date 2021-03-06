#include <GL/glew.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Logger/Logger.hpp"
#include "Shader/Shader.hpp"
#include "Program/Program.hpp"
#include "Util/Util.hpp"
#include "Light/Light.hpp"
#include <thread>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <locale>
#include <chrono>
#include <algorithm>
#include <array>
#include <ctime>
#include <random>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ext/stb_image_write.h"

constexpr const_vec<float> init_win_size(960.f, 540.f);
constexpr const_vec<float> render_size(3840.f, 2160.f);

// Camera struct
struct camera {
	glm::quat orientation;
	glm::vec3 position;

	void rotate(glm::vec3 axis, float angle) {
		glm::quat rot = glm::angleAxis(angle, axis);
		orientation = rot * orientation;
		orientation = normalize(orientation);
	}
	void strafe(float amount) {
		move({0.f, amount, 0.f});
	}
	void climb(float amount) {
		move({0.f, 0.f, amount});
	}
	void advance(float amount) {
		move({amount, 0.f, 0.f});
	}
	void move(glm::vec3 delta) {
		auto tmp = glm::mat4_cast(orientation);
		auto x = glm::vec3(tmp[0][2], tmp[1][2], tmp[2][2]);
		auto y = -glm::vec3(tmp[0][0], tmp[1][0], tmp[2][0]);
		auto z = -glm::vec3(tmp[0][1], tmp[1][1], tmp[2][1]);
		position += x*delta.x + y*delta.y + z*delta.z;
	}

	glm::mat4 get_view() {
		auto tmp = glm::mat4_cast(orientation);
		tmp = glm::translate(tmp, position);
		return tmp;
	}
};

GLuint framebuffer_display_color_texture;

constexpr float pi = 3.14159;

Logger<wchar_t> wlog{std::wcout};

Shader *shader_render_vert;
Shader *shader_render_tcs;
Shader *shader_render_tes;
Shader *shader_render_geom;
Shader *shader_render_frag;
Shader *shader_lighting_vert;
Shader *shader_lighting_frag;
Shader *shader_display_vert;
Shader *shader_display_frag;
Program *render_program;
Program *lighting_program;
Program *display_program;

bool shaders_reloaded = false;
bool limit_fps = true;
bool lighting = true;
bool draw_water = true;
bool draw_land = true;

bool load_shaders() {
	shader_render_vert = new Shader;
	shader_render_tcs = new Shader;
	shader_render_tes = new Shader;
	shader_render_geom = new Shader;
	shader_render_frag = new Shader;
	shader_lighting_vert = new Shader;
	shader_lighting_frag = new Shader;
	shader_display_vert = new Shader;
	shader_display_frag = new Shader;
	render_program = new Program;
	lighting_program = new Program;
	display_program = new Program;


	wlog.log(L"Creating Shaders.\n");

	wlog.log(L"Creating render vertex shader.\n");
	shader_render_vert->load_file(GL_VERTEX_SHADER, "assets/shaders/render/shader.vert");

	wlog.log(L"Creating render TCS shader.\n");
	shader_render_tes->load_file(GL_TESS_CONTROL_SHADER, "assets/shaders/render/shader.tcs");

	wlog.log(L"Creating render geometry shader.\n");
	shader_render_tcs->load_file(GL_TESS_EVALUATION_SHADER, "assets/shaders/render/shader.tes");

	wlog.log(L"Creating render geometry shader.\n");
	shader_render_geom->load_file(GL_GEOMETRY_SHADER, "assets/shaders/render/shader.geom");

	wlog.log(L"Creating render fragment shader.\n");
	shader_render_frag->load_file(GL_FRAGMENT_SHADER, "assets/shaders/render/shader.frag");

	wlog.log(L"Creating and linking render shader program.\n");

	render_program->attach(*shader_render_vert);
	render_program->attach(*shader_render_tcs);
	render_program->attach(*shader_render_tes);
	render_program->attach(*shader_render_geom);
	render_program->attach(*shader_render_frag);
	glBindFragDataLocation(*render_program, 0, "outColor");
	glBindFragDataLocation(*render_program, 1, "outNormal");
	render_program->link();


	wlog.log(L"Creating lighting vertex shader.\n");
	shader_lighting_vert->load_file(GL_VERTEX_SHADER, "assets/shaders/lighting/shader.vert");

	wlog.log(L"Creating lighting fragment shader.\n");
	shader_lighting_frag->load_file(GL_FRAGMENT_SHADER, "assets/shaders/lighting/shader.frag");


	wlog.log(L"Creating and linking lighting shader program.\n");

	
	lighting_program->attach(*shader_lighting_vert);
	lighting_program->attach(*shader_lighting_frag);
	glBindFragDataLocation(*lighting_program, 0, "outCol");
	lighting_program->link();


	wlog.log(L"Creating display vertex shader.\n");
	
	shader_display_vert->load_file(GL_VERTEX_SHADER, "assets/shaders/display/shader.vert");

	wlog.log(L"Creating display fragment shader.\n");
	
	shader_display_frag->load_file(GL_FRAGMENT_SHADER, "assets/shaders/display/shader.frag");

	wlog.log(L"Creating and linking display shader program.\n");

	
	display_program->attach(*shader_display_vert);
	display_program->attach(*shader_display_frag);
	glBindFragDataLocation(*display_program, 0, "color");
	display_program->link();
	return true;
}

bool destroy_shaders() {
	delete shader_render_frag;
	delete shader_render_tcs;
	delete shader_render_tes;
	delete shader_render_geom;
	delete shader_render_vert;
	delete shader_lighting_frag;
	delete shader_lighting_vert;
	delete shader_display_frag;
	delete shader_display_vert;
	delete render_program;
	delete lighting_program;
	delete display_program;
	return true;
}

bool reload_shaders() {
	destroy_shaders();
	load_shaders();
	return true;
}

bool readfile(const char* filename, std::string &contents);
bool process_gl_errors();

int main()
{
	using namespace std::literals::chrono_literals;

	wlog.log(L"Starting up.\n");
	wlog.log(L"Initializing GLFW.\n");

	if(!glfwInit())
		return -1;

	glfwSetErrorCallback(
		[](int, const char* msg){
			wlog.log(std::wstring{msg, msg+std::strlen(msg)}+L"\n");
		}
	);

	// Create window with context params etc.
	wlog.log(L"Creating window.\n");
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_DEPTH_BITS, 32);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	GLFWwindow *win = glfwCreateWindow(
		init_win_size.x, init_win_size.y, "infiniterrain", nullptr, nullptr
	);

	// If window creation fails, exit.
	if(!win)
		return -2;

	int win_size[2];
	int &win_size_x=win_size[0], &win_size_y=win_size[1];
	glfwGetWindowSize(win, &win_size_x, &win_size_y);

	glfwMakeContextCurrent(win);

	wlog.log(L"Initializing GLEW.\n");
	glewExperimental = GL_TRUE;
	if(glewInit())
		return -3;

	process_gl_errors();

	wlog.log(
		L"Any errors produced directly after GLEW initialization "
		L"should be ignorable.\n"
	);

	wlog.log("Generating Vertex Array Object.\n");
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	load_shaders();

	process_gl_errors();

	glUseProgram(*render_program);

	glViewport(0.f, 0.f, win_size_x, win_size_y);


	wlog.log(L"Creating and getting camera position uniform data.\n");

	camera cam;

	cam.position = glm::vec3(0.f, 69.f, -20.f);
	cam.rotate(glm::vec3(1.f, 0.f, 0.f), -pi/3.f);

	process_gl_errors();

	wlog.log(L"Creating and getting view uniform data.\n");
	glm::mat4 view = cam.get_view();

	GLint camera_position_uni = glGetUniformLocation(*render_program, "camera_position");
	glUniform3fv(camera_position_uni, 1, glm::value_ptr(cam.position));

	GLint draw_water_uni = glGetUniformLocation(*render_program, "draw_water");
	glUniform1i(draw_water_uni, 0);

	GLint view_uni = glGetUniformLocation(*render_program, "view");
	glUniformMatrix4fv(view_uni, 1, GL_FALSE, glm::value_ptr(view));

	process_gl_errors();


	wlog.log(L"Creating and getting projection uniform data.\n");
	glm::mat4 projection = glm::perspective(
		pi/3.f, render_size.x/render_size.y, 0.01f, 3000.0f
	);
	GLint projection_uni = glGetUniformLocation(*render_program, "projection");
	glUniformMatrix4fv(projection_uni, 1, GL_FALSE, glm::value_ptr(projection));

	GLint render_spritesheet_uni = glGetUniformLocation(*render_program, "spritesheet");

	process_gl_errors();

	glUseProgram(*lighting_program);

	GLint light_color_uni = glGetUniformLocation(*lighting_program, "colorTex");
	GLint light_normals_uni = glGetUniformLocation(*lighting_program, "normalsTex");
	GLint light_depth_uni = glGetUniformLocation(*lighting_program, "depthTex");
	GLint light_proj_uni = glGetUniformLocation(*lighting_program, "projection");
	glUniformMatrix4fv(light_proj_uni, 1, GL_FALSE, glm::value_ptr(projection));
	GLint light_view_uni = glGetUniformLocation(*lighting_program, "view");
	glUniformMatrix4fv(light_view_uni, 1, GL_FALSE, glm::value_ptr(view));

	LightArray lights;
	lights.light_count = 1;
	lights.lights[0].brightness = 1.7f;
	lights.lights[0].radius = 500.f;
	lights.lights[0].fade = 30.f;
	lights.lights[0].position = glm::vec4(8.f, 8.f, 70.f, 1.f);
	lights.lights[0].color = glm::vec4(1.f, 0.9f, 1.f, 1.f);
	GLuint light_buffer;
	glGenBuffers(1, &light_buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, light_buffer);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(lights), &lights, GL_STREAM_COPY);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, light_buffer);


	GLint light_intensity_uni = glGetUniformLocation(*lighting_program, "intensity");
	GLint light_bias_uni = glGetUniformLocation(*lighting_program, "bias");
	GLint light_rad_uni = glGetUniformLocation(*lighting_program, "sample_radius");
	GLint light_scale_uni = glGetUniformLocation(*lighting_program, "scale");

	process_gl_errors();

	wlog.log(L"Starting main loop.\n");

	// glEnable(GL_CULL_FACE);
	// glFrontFace(GL_CW);
	// glCullFace(GL_BACK);

	glEnable(GL_FRAMEBUFFER_SRGB);

	GLuint framebuffer_render;
	glGenFramebuffers(1, &framebuffer_render);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_render);

	GLuint framebuffer_render_color_texture;
	glGenTextures(1, &framebuffer_render_color_texture);
	glActiveTexture(GL_TEXTURE0+4);
	glProgramUniform1i(*lighting_program, light_color_uni, 4);
	glBindTexture(GL_TEXTURE_2D, framebuffer_render_color_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, render_size.x, render_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glEnablei(GL_BLEND, framebuffer_render_color_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, framebuffer_render_color_texture, 0);

	GLuint framebuffer_render_normals_texture;
	glGenTextures(1, &framebuffer_render_normals_texture);
	glActiveTexture(GL_TEXTURE0+5);
	glProgramUniform1i(*lighting_program, light_normals_uni, 5);
	glBindTexture(GL_TEXTURE_2D, framebuffer_render_normals_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, render_size.x, render_size.y, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, framebuffer_render_normals_texture, 0);
	
	GLuint framebuffer_render_depth_texture;
	glGenTextures(1, &framebuffer_render_depth_texture);
	glActiveTexture(GL_TEXTURE0+6);
	glProgramUniform1i(*lighting_program, light_depth_uni, 6);
	glBindTexture(GL_TEXTURE_2D, framebuffer_render_depth_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, render_size.x, render_size.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, framebuffer_render_depth_texture, 0);

	GLenum drawbuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_DEPTH_ATTACHMENT};
	glDrawBuffers(2, drawbuffers);

	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		wlog.log("Incomplete framebuffer!\n");

	GLuint framebuffer_display;
	glGenFramebuffers(1, &framebuffer_display);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_display);

	GLint framebuffer_uni = glGetUniformLocation(*display_program, "framebuffer");

	glGenTextures(1, &framebuffer_display_color_texture);
	glActiveTexture(GL_TEXTURE0+7);
	glProgramUniform1i(*display_program, framebuffer_uni, 7);
	glBindTexture(GL_TEXTURE_2D, framebuffer_display_color_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB_ALPHA, render_size.x, render_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, framebuffer_display_color_texture, 0);

	float fb_vertices[] = {
		// Coords  Texcoords
		-1.f,  1.f,   0.f, 1.f,
		 1.f, -1.f,   1.f, 0.f,
		-1.f, -1.f,   0.f, 0.f,
		 1.f,  1.f,   1.f, 1.f,
		 1.f, -1.f,   1.f, 0.f,
		-1.f,  1.f,   0.f, 1.f,
	};
	GLuint fb_vbo, fb_vao;
	glGenVertexArrays(1, &fb_vao);
	glBindVertexArray(fb_vao);
	glGenBuffers(1, &fb_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, fb_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(fb_vertices), fb_vertices, GL_STATIC_DRAW);

	GLint fb_vao_pos_attrib = glGetAttribLocation(*display_program, "pos");
	if(fb_vao_pos_attrib != -1) {
		glEnableVertexAttribArray(fb_vao_pos_attrib);
		glVertexAttribPointer(fb_vao_pos_attrib, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
	}

	GLint fb_vao_texcoord_attrib = glGetAttribLocation(*display_program, "texcoords");
	if(fb_vao_texcoord_attrib != -1) {
		glEnableVertexAttribArray(fb_vao_texcoord_attrib);
		glVertexAttribPointer(fb_vao_texcoord_attrib, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), BUFFER_OFFSET(sizeof(float)*2));
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glUseProgram(*render_program);

	glm::vec2 map_size(200.f, 200.f);

	float multiplier = 10.f;

	std::vector<glm::vec2> map(int(map_size.x * map_size.y * 3 * 2));

	for(int x = 0; x < map_size.x; ++x) {
		for(int y = 0; y < map_size.y; ++y) {
			map[(x*map_size.y + y)*3*2 + 0] = (glm::vec2(  x, y  ) - map_size*0.5f) * multiplier;
			map[(x*map_size.y + y)*3*2 + 1] = (glm::vec2(x+1, y  ) - map_size*0.5f) * multiplier;
			map[(x*map_size.y + y)*3*2 + 2] = (glm::vec2(x  , y+1) - map_size*0.5f) * multiplier;
			map[(x*map_size.y + y)*3*2 + 3] = (glm::vec2(x  , y+1) - map_size*0.5f) * multiplier;
			map[(x*map_size.y + y)*3*2 + 4] = (glm::vec2(x+1, y  ) - map_size*0.5f) * multiplier;
			map[(x*map_size.y + y)*3*2 + 5] = (glm::vec2(x+1, y+1) - map_size*0.5f) * multiplier;
		}
	}

	glPatchParameteri(GL_PATCH_VERTICES, 3);

	glUseProgram(*render_program);

	GLuint map_vbo;
	glGenBuffers(1, &map_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, map_vbo);
	glBufferData(GL_ARRAY_BUFFER, map.size() * sizeof(glm::vec2), map.data(), GL_STATIC_DRAW);

	GLuint map_vao;
	glGenVertexArrays(1, &map_vao);
	glBindVertexArray(map_vao);
	glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), BUFFER_OFFSET(0));
	glEnableVertexAttribArray(4);

	glfwSetKeyCallback(win, [](GLFWwindow*, int key, int, int action, int){
		switch(action) {
			case GLFW_PRESS: {
				switch(key) {

				}
			} break;
			case GLFW_RELEASE: {
				switch(key) {
					case GLFW_KEY_F: {
						static bool wireframe=false;
						wireframe = !wireframe;
						if(wireframe)
							glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
						else
							glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
					} break;
					case GLFW_KEY_U: {
						uint8_t *pixels = new uint8_t[static_cast<int>(render_size.x)*static_cast<int>(render_size.y)*4];
						glBindTexture(GL_TEXTURE_2D, framebuffer_display_color_texture);
						glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
						uint8_t *topbottom_pixels = new uint8_t[static_cast<int>(render_size.x)*static_cast<int>(render_size.y)*4];
						for(int y = 0; y < render_size.y; ++y) {
							for(int x = 0; x < render_size.x; ++x) {
								int ny = (render_size.y-1) - y;
								topbottom_pixels[(x+y*int(render_size.x))*4+0] = pixels[(x+ny*int(render_size.x))*4+0];
								topbottom_pixels[(x+y*int(render_size.x))*4+1] = pixels[(x+ny*int(render_size.x))*4+1];
								topbottom_pixels[(x+y*int(render_size.x))*4+2] = pixels[(x+ny*int(render_size.x))*4+2];
								topbottom_pixels[(x+y*int(render_size.x))*4+3] = pixels[(x+ny*int(render_size.x))*4+3];
							}
						}
						if(!stbi_write_png("/tmp/screenshot.png", render_size.x, render_size.y, 4, topbottom_pixels, 0)) {
							wlog.log(L"ERROR SAVING SCREENSHOT!\n");
						}
						else {
							wlog.log(L"Screenshot saved to /tmp/screenshot.png \n");
						}
						delete[] pixels;
					} break;
					case GLFW_KEY_R: {
						//Reload shaders
						reload_shaders();
						shaders_reloaded = true;
					} break;
					case GLFW_KEY_P: {
						limit_fps = !limit_fps;
					} break;
					case GLFW_KEY_L: {
						lighting = !lighting;
					} break;
					case GLFW_KEY_O: {
						draw_water = !draw_water;
					} break;
					case GLFW_KEY_I: {
						draw_land = !draw_land;
					} break;
				}
			} break;
		}
	});

	std::chrono::high_resolution_clock::time_point start, end, timetoprint;
	timetoprint = end = start = std::chrono::high_resolution_clock::now();

	long long cnt=0;
	long double ft_total=0.f;

	glClearColor(0.517f, 0.733f, 0.996f, 1.f);

	float intensity = 0.91;
	float bias = 0.21;
	float scale = 0.27;
	float sample_radius = 0.20;

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendEquation(GL_FUNC_ADD);
	// glBlendFuncSeparate(GL_SRC_COLOR, GL_ZERO, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

	while(!glfwWindowShouldClose(win)) {
		end = std::chrono::high_resolution_clock::now();
		long int ft = std::chrono::duration_cast<std::chrono::microseconds>(
			end-start
		).count();
		double fts = static_cast<double>(ft)/1e6L;
		float fts_float = static_cast<float>(fts);
		ft_total += ft;
		++cnt;
		auto tslastprint = std::chrono::duration_cast<std::chrono::seconds>(
			end-timetoprint
		).count();
		if(tslastprint >= 1) {
			timetoprint = std::chrono::high_resolution_clock::now();
			float ft_avg = ft_total/cnt;
			std::wstring frametimestr = L"FPS avg: " + 
				std::to_wstring(1e6L/ft_avg) + L"\t" +
				L"Frametime avg: "+std::to_wstring(ft_avg)+L"µs\n";
			wlog.log(frametimestr);
			cnt=0;
			ft_total=0.L;
			wlog.log(L"Position: {" + std::to_wstring(cam.position.x) + std::to_wstring(cam.position.y) + std::to_wstring(cam.position.z) + L"}\n");
			// wlog.log(L"SSAO Intensity : \t" + std::to_wstring(intensity) + L"\n");
			// wlog.log(L"SSAO Bias : \t" + std::to_wstring(bias) + L"\n");
			// wlog.log(L"SSAO Scale : \t" + std::to_wstring(scale) + L"\n");
			// wlog.log(L"SSAO Sample Radius : \t" + std::to_wstring(sample_radius) + L"\n");
		}
		start=end;

		if(shaders_reloaded) {
			shaders_reloaded = false;

			glUseProgram(*render_program);

			view_uni = glGetUniformLocation(*render_program, "view");
			glUniformMatrix4fv(view_uni, 1, GL_FALSE, glm::value_ptr(view));

			camera_position_uni = glGetUniformLocation(*render_program, "camera_position");
			glUniform3fv(camera_position_uni, 1, glm::value_ptr(cam.position));

			GLint draw_water_uni = glGetUniformLocation(*render_program, "draw_water");
			glUniform1i(draw_water_uni, 0);

			projection_uni = glGetUniformLocation(*render_program, "projection");
			glUniformMatrix4fv(projection_uni, 1, GL_FALSE, glm::value_ptr(projection));

			render_spritesheet_uni = glGetUniformLocation(*render_program, "spritesheet");

			glUseProgram(*lighting_program);

			light_color_uni = glGetUniformLocation(*lighting_program, "colorTex");
			light_normals_uni = glGetUniformLocation(*lighting_program, "normalsTex");
			light_depth_uni = glGetUniformLocation(*lighting_program, "depthTex");
			light_proj_uni = glGetUniformLocation(*lighting_program, "projection");
			glUniformMatrix4fv(light_proj_uni, 1, GL_FALSE, glm::value_ptr(projection));
			light_view_uni = glGetUniformLocation(*lighting_program, "view");
			glUniformMatrix4fv(light_view_uni, 1, GL_FALSE, glm::value_ptr(view));


			light_intensity_uni = glGetUniformLocation(*lighting_program, "intensity");
			light_bias_uni = glGetUniformLocation(*lighting_program, "bias");
			light_rad_uni = glGetUniformLocation(*lighting_program, "sample_radius");
			light_scale_uni = glGetUniformLocation(*lighting_program, "scale");

			glProgramUniform1i(*lighting_program, light_color_uni, 4);
			glProgramUniform1i(*lighting_program, light_normals_uni, 5);
			glProgramUniform1i(*lighting_program, light_depth_uni, 6);
			glProgramUniform1i(*display_program, framebuffer_uni, 7);

			glBindBuffer(GL_ARRAY_BUFFER, map_vbo);
			glBindVertexArray(map_vao);
			glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), BUFFER_OFFSET(0));
			glEnableVertexAttribArray(4);

			glBindBuffer(GL_ARRAY_BUFFER, fb_vbo);
			glBindVertexArray(fb_vao);

			fb_vao_pos_attrib = glGetAttribLocation(*display_program, "pos");
			if(fb_vao_pos_attrib != -1) {
				glEnableVertexAttribArray(fb_vao_pos_attrib);
				glVertexAttribPointer(fb_vao_pos_attrib, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
			}

			fb_vao_texcoord_attrib = glGetAttribLocation(*display_program, "texcoords");
			if(fb_vao_texcoord_attrib != -1) {
				glEnableVertexAttribArray(fb_vao_texcoord_attrib);
				glVertexAttribPointer(fb_vao_texcoord_attrib, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), BUFFER_OFFSET(sizeof(float)*2));
			}
		}

		float mult = 1.0f;

		if(glfwGetKey(win, GLFW_KEY_LEFT_SHIFT)) {
			mult = 0.3f;
		}

		if(glfwGetKey(win, GLFW_KEY_W)) {
			cam.rotate({-1.f, 0.f, 0.f}, fts_float*3.f);
		}
		if(glfwGetKey(win, GLFW_KEY_S)) {
			cam.rotate({ 1.f, 0.f, 0.f}, fts_float*3.f);
		}
		if(glfwGetKey(win, GLFW_KEY_A)) {
			cam.rotate({ 0.f,-1.f, 0.f}, fts_float*3.f);
		}
		if(glfwGetKey(win, GLFW_KEY_D)) {
			cam.rotate({ 0.f, 1.f, 0.f}, fts_float*3.f);
		}
		if(glfwGetKey(win, GLFW_KEY_Q)) {
			cam.rotate({ 0.f, 0.f,-1.f}, fts_float*3.f);
		}
		if(glfwGetKey(win, GLFW_KEY_E)) {
			cam.rotate({ 0.f, 0.f, 1.f}, fts_float*3.f);
		}
		if(glfwGetKey(win, GLFW_KEY_UP)) {
			cam.advance(mult*fts_float*100.f);
		}
		if(glfwGetKey(win, GLFW_KEY_DOWN)) {
			cam.advance(mult*fts_float*-100.f);
		}
		if(glfwGetKey(win, GLFW_KEY_RIGHT)) {
			cam.strafe(mult*fts_float*100.f);
		}
		if(glfwGetKey(win, GLFW_KEY_LEFT)) {
			cam.strafe(mult*fts_float*-100.f);
		}
		if(glfwGetKey(win, GLFW_KEY_SPACE)) {
			cam.climb(mult*fts_float*100.f);
		}
		if(glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL)) {
			cam.climb(mult*fts_float*-100.f);
		}

		if(glfwGetKey(win, GLFW_KEY_G)) {
			intensity += 0.01;
			glProgramUniform1f(*lighting_program, light_intensity_uni, intensity);
		}
		if(glfwGetKey(win, GLFW_KEY_V)) {
			intensity -= 0.01;
			glProgramUniform1f(*lighting_program, light_intensity_uni, intensity);
		}
		if(glfwGetKey(win, GLFW_KEY_H)) {
			bias += 0.01;
			glProgramUniform1f(*lighting_program, light_bias_uni, bias);
		}
		if(glfwGetKey(win, GLFW_KEY_B)) {
			bias -= 0.01;
			glProgramUniform1f(*lighting_program, light_bias_uni, bias);
		}
		if(glfwGetKey(win, GLFW_KEY_J)) {
			sample_radius += 0.01;
			glProgramUniform1f(*lighting_program, light_rad_uni, sample_radius);
		}
		if(glfwGetKey(win, GLFW_KEY_N)) {
			sample_radius -= 0.01;
			glProgramUniform1f(*lighting_program, light_rad_uni, sample_radius);
		}
		if(glfwGetKey(win, GLFW_KEY_K)) {
			scale += 0.01;
			glProgramUniform1f(*lighting_program, light_scale_uni, scale);
		}
		if(glfwGetKey(win, GLFW_KEY_M)) {
			scale -= 0.01;
			glProgramUniform1f(*lighting_program, light_scale_uni, scale);
		}

		glUseProgram(*render_program);

		view = cam.get_view();
		glUniform3fv(camera_position_uni, 1, glm::value_ptr(cam.position));
		glUniformMatrix4fv(view_uni, 1, GL_FALSE, glm::value_ptr(view));
		glProgramUniformMatrix4fv(*lighting_program, light_view_uni, 1, GL_FALSE, glm::value_ptr(view));
		glUniform1i(render_spritesheet_uni, 0);


		if(lighting) {
			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_render);
			glViewport(0.f, 0.f, render_size.x, render_size.y);
		}
		else {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glBindBuffer(GL_ARRAY_BUFFER, map_vbo);
		glBindVertexArray(map_vao);

		if(draw_land) {
			glUniform1i(draw_water_uni, 0);
			glDrawArrays(GL_PATCHES, 0, map.size());
			// glDrawArrays(GL_TRIANGLES, 0, map.size());
		}
		if(draw_water) {
			glUniform1i(draw_water_uni, 1);
			glDrawArrays(GL_PATCHES, 0, map.size());
			// glDrawArrays(GL_TRIANGLES, 0, map.size());
		}

		if(lighting) {
			glDisable(GL_DEPTH_TEST);
			
			GLint poly_mode;

			glGetIntegerv(GL_POLYGON_MODE, &poly_mode);

			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

			glBindVertexArray(fb_vao);
			glBindBuffer(GL_ARRAY_BUFFER, fb_vbo);

			glUseProgram(*lighting_program);

			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_display);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glDrawArrays(GL_TRIANGLES, 0, 6);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glUseProgram(*display_program);
			glfwGetWindowSize(win, &win_size_x, &win_size_y);
			glViewport(0.f, 0.f, win_size_x, win_size_y);

			glDrawArrays(GL_TRIANGLES, 0, 6);

			glPolygonMode(GL_FRONT_AND_BACK, poly_mode);

			glEnable(GL_DEPTH_TEST);
		}

		glfwSwapBuffers(win);
		glfwPollEvents();
		process_gl_errors();

		if(limit_fps)
			std::this_thread::sleep_for(std::chrono::milliseconds(60));
	}

	glfwDestroyWindow(win);

	return 0;
}

bool process_gl_errors()
{
	GLenum gl_err;
	bool no_err=true;
	while((gl_err = glGetError()) != GL_NO_ERROR) {
		no_err=false;
		wlog.log("OpenGL Error:\n");
		switch(gl_err) {
			case GL_INVALID_ENUM:
				wlog.log("\tInvalid enum.\n");
				break;
			case GL_INVALID_VALUE:
				wlog.log("\tInvalid value.\n");
				break;
			case GL_INVALID_OPERATION:
				wlog.log("\tInvalid operation.\n");
				break;
			case GL_INVALID_FRAMEBUFFER_OPERATION:
				wlog.log("\tInvalid framebuffer operation.\n");
				break;
			case GL_OUT_OF_MEMORY:
				wlog.log("\tOut of memory.\n");
				break;
		}
	}
	return no_err;
}
