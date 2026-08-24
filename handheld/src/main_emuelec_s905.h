#ifndef MAIN_EMUELEC_S905_H__
#define MAIN_EMUELEC_S905_H__

#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <png.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <SDL/SDL.h>

#define check() assert(glGetError() == 0)

#include "App.h"
#include "platform/input/Mouse.h"
#include "platform/input/Multitouch.h"
#include "platform/input/Keyboard.h"

int width = 1280;
int height = 720;

static void png_funcReadFile(png_structp pngPtr, png_bytep data, png_size_t length) {
	((std::istream*)png_get_io_ptr(pngPtr))->read((char*)data, length);
}

class AppPlatform_EmuELEC_S905: public AppPlatform
{
public:
    bool isTouchscreen()  { return false; }

    TextureData loadTexture(const std::string& filename_, bool textureFolder)
    {
		TextureData out;

		std::string filename = textureFolder? "data/images/" + filename_
											: filename_;
		std::ifstream source(filename.c_str(), std::ios::binary);

		if (source) {
			png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

			if (!pngPtr)
				return out;

			png_infop infoPtr = png_create_info_struct(pngPtr);

			if (!infoPtr) {
				png_destroy_read_struct(&pngPtr, NULL, NULL);
				return out;
			}

			png_set_read_fn(pngPtr,(voidp)&source, png_funcReadFile);
			png_read_info(pngPtr, infoPtr);

			// Set up the texdata properties
			out.w = png_get_image_width(pngPtr, infoPtr);
			out.h = png_get_image_height(pngPtr, infoPtr);

			// Check S905 Mali texture limits (2048x2048 max)
			if (out.w > 2048 || out.h > 2048) {
				LOGI("WARNING: Texture %s exceeds S905 Mali limit (%d x %d). Consider resizing.\n", 
					 filename.c_str(), out.w, out.h);
			}

			png_bytep* rowPtrs = new png_bytep[out.h];
			out.data = new unsigned char[4 * out.w * out.h];
			out.memoryHandledExternally = false;

			int rowStrideBytes = 4 * out.w;
			for (int i = 0; i < out.h; i++) {
				rowPtrs[i] = (png_bytep)&out.data[i*rowStrideBytes];
			}
			png_read_image(pngPtr, rowPtrs);

			// Teardown and return
			png_destroy_read_struct(&pngPtr, &infoPtr,(png_infopp)0);
			delete[] (png_bytep)rowPtrs;
			source.close();

			return out;
		}
		else
		{
			LOGI("Couldn't find file: %s\n", filename.c_str());
			return out;
		}
    }
};

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLContext g_context = EGL_NO_CONTEXT;
static EGLSurface g_surface = EGL_NO_SURFACE;
static bool g_egl_initialized = false;
static bool g_app_initialized = false;

// Joystick input state
static SDL_Joystick* g_joystick = NULL;
static const float JOYSTICK_DEADZONE = 0.3f;
static bool g_key_state[256] = {0};  // Track key states for analog stick

static void initEGL(App* app, AppContext* state, uint32_t w, uint32_t h)
{
	if (g_egl_initialized) return;

	const EGLint attribs[] = {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	const EGLint contextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	assert(g_display != EGL_NO_DISPLAY);
	check();

	EGLint majorVersion, minorVersion;
	EGLBoolean result = eglInitialize(g_display, &majorVersion, &minorVersion);
	assert(EGL_FALSE != result);
	check();

	LOGI("EGL version: %d.%d\n", majorVersion, minorVersion);

	EGLConfig config;
	EGLint configCount;
	result = eglChooseConfig(g_display, attribs, &config, 1, &configCount);
	assert(EGL_FALSE != result);
	check();

	result = eglBindAPI(EGL_OPENGL_ES_API);
	assert(EGL_FALSE != result);
	check();

	g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, contextAttribs);
	assert(g_context != EGL_NO_CONTEXT);
	check();

	// EmuELEC typically uses native window rendering
	g_surface = eglCreateWindowSurface(g_display, config, (EGLNativeWindowType)0, NULL);
	if (g_surface == EGL_NO_SURFACE) {
		LOGE("Failed to create EGL window surface. Trying pbuffer surface.\n");
		const EGLint pbufferAttribs[] = {
			EGL_WIDTH, w,
			EGL_HEIGHT, h,
			EGL_NONE
		};
		g_surface = eglCreatePbufferSurface(g_display, config, pbufferAttribs);
		assert(g_surface != EGL_NO_SURFACE);
	}
	check();

	result = eglMakeCurrent(g_display, g_surface, g_surface, g_context);
	assert(EGL_FALSE != result);
	check();

	eglSwapInterval(g_display, 1);  // 60 FPS vsync

	g_egl_initialized = true;

	// Log GPU info
	LOGI("OpenGL Vendor: %s\n", glGetString(GL_VENDOR));
	LOGI("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));
	LOGI("OpenGL Version: %s\n", glGetString(GL_VERSION));
	LOGI("OpenGL ES Shading Language Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	if (!g_app_initialized) {
		g_app_initialized = true;
		app->init(*state);
	} else {
		app->onGraphicsReset(*state);
	}
	app->setSize(w, h);
}

static void deinitEGL()
{
	if (!g_egl_initialized) return;

	if (g_surface != EGL_NO_SURFACE) {
		eglSwapBuffers(g_display, g_surface);
		eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroySurface(g_display, g_surface);
		g_surface = EGL_NO_SURFACE;
	}

	if (g_context != EGL_NO_CONTEXT) {
		eglDestroyContext(g_display, g_context);
		g_context = EGL_NO_CONTEXT;
	}

	if (g_display != EGL_NO_DISPLAY) {
		eglTerminate(g_display);
		g_display = EGL_NO_DISPLAY;
	}

	g_egl_initialized = false;
}

static unsigned char transformKey(int key) {
	// Handle ALL keys here. If not handled -> return 0 ("invalid")
	if (key == SDLK_LSHIFT) return Keyboard::KEY_LSHIFT;
	if (key == SDLK_DOWN) return 40;
	if (key == SDLK_UP)   return 38;
	if (key == SDLK_SPACE) return Keyboard::KEY_SPACE;
	if (key == SDLK_RETURN) return 13;
	if (key == SDLK_ESCAPE) return Keyboard::KEY_ESCAPE;
	if (key == SDLK_TAB) return 250;
	if (key >= 'a' && key <= 'z') return key - 32;
	if (key >= SDLK_0 && key <= SDLK_9) return '0' + (key - SDLK_0);
	return 0;
}

// Joystick button mapping for standard gamepad layout
static void handleJoystickButton(int button, int pressed)
{
	// Standard XInput-style button mapping (common on EmuELEC):
	// Button 0 = A (South), 1 = B (East), 2 = X (West), 3 = Y (North)
	// Button 4 = LB (Left Shoulder), 5 = RB (Right Shoulder)
	// Button 6 = Back, 7 = Start
	// Button 8 = Left Stick Click, 9 = Right Stick Click

	LOGI("Joystick button: %d, pressed: %d\n", button, pressed);

	switch(button) {
		case 0:  // A button -> Jump/Attack (SPACE)
			Keyboard::feed(Keyboard::KEY_SPACE, pressed);
			break;
		
		case 1:  // B button -> Sneak/Crouch (LSHIFT)
			Keyboard::feed(Keyboard::KEY_LSHIFT, pressed);
			break;
		
		case 2:  // X button -> Right Click / Use Item
			Mouse::feed(2, pressed, 0, 0);
			break;
		
		case 3:  // Y button -> Left Click / Break Block
			Mouse::feed(1, pressed, 0, 0);
			break;
		
		case 4:  // LB -> Inventory or modifier (E key)
			Keyboard::feed('E', pressed);
			break;
		
		case 5:  // RB -> Chat or interaction (T key)
			Keyboard::feed('T', pressed);
			break;
		
		case 6:  // Back -> Escape/Menu
			Keyboard::feed(Keyboard::KEY_ESCAPE, pressed);
			break;
		
		case 7:  // Start -> Pause (ESC)
			Keyboard::feed(Keyboard::KEY_ESCAPE, pressed);
			break;
		
		case 8:  // Left Stick Click -> Sprint (LCTRL)
			Keyboard::feed(17, pressed);  // CTRL
			break;
		
		case 9:  // Right Stick Click -> Toggle perspective (F5)
			Keyboard::feed('F', pressed);  // Can map to a creative mode toggle
			break;

		default:
			LOGI("Unknown button: %d\n", button);
			break;
	}
}

// Joystick analog stick and trigger mapping
static void handleJoystickAxis(int axis, float value)
{
	// Apply deadzone
	if (std::fabs(value) < JOYSTICK_DEADZONE) {
		value = 0.0f;
	}

	// Axis mapping (common on EmuELEC controllers):
	// 0 = Left Stick X (Strafe A/D)
	// 1 = Left Stick Y (Forward/Back W/S)
	// 2 = Right Stick X (Look Left/Right - mouse X)
	// 3 = Right Stick Y (Look Up/Down - mouse Y)
	// 4 = Left Trigger (LT)
	// 5 = Right Trigger (RT)

	switch(axis) {
		case 0:  // Left stick X - Strafe (A/D)
		{
			// Release previous key
			if (g_key_state['A']) {
				Keyboard::feed('A', 0);
				g_key_state['A'] = false;
			}
			if (g_key_state['D']) {
				Keyboard::feed('D', 0);
				g_key_state['D'] = false;
			}

			// Press new key based on direction
			if (value < -0.3f) {
				Keyboard::feed('A', 1);
				g_key_state['A'] = true;
			} else if (value > 0.3f) {
				Keyboard::feed('D', 1);
				g_key_state['D'] = true;
			}
			break;
		}

		case 1:  // Left stick Y - Forward/Back (W/S)
		{
			// Release previous key
			if (g_key_state['W']) {
				Keyboard::feed('W', 0);
				g_key_state['W'] = false;
			}
			if (g_key_state['S']) {
				Keyboard::feed('S', 0);
				g_key_state['S'] = false;
			}

			// Press new key based on direction
			// Note: Y axis is often inverted (negative = up in SDL)
			if (value < -0.3f) {
				Keyboard::feed('W', 1);
				g_key_state['W'] = true;
			} else if (value > 0.3f) {
				Keyboard::feed('S', 1);
				g_key_state['S'] = true;
			}
			break;
		}

		case 2:  // Right stick X - Look horizontal (mouse movement)
		{
			int mouseX = (int)(value * 100.0f);  // Scale to reasonable mouse delta
			if (mouseX != 0) {
				Mouse::feed(0, 0, 0, 0, mouseX, 0);
			}
			break;
		}

		case 3:  // Right stick Y - Look vertical (mouse movement)
		{
			int mouseY = (int)(value * 100.0f);
			if (mouseY != 0) {
				Mouse::feed(0, 0, 0, 0, 0, mouseY);
			}
			break;
		}

		case 4:  // Left Trigger (LT) - Sprint modifier
		case 5:  // Right Trigger (RT) - Sprint modifier
		{
			if (value > 0.5f) {
				// Trigger is pressed, enable sprint
				Keyboard::feed(17, 1);  // CTRL for sprint
				g_key_state['_'] = true;
			} else if (g_key_state['_']) {
				// Trigger released
				Keyboard::feed(17, 0);
				g_key_state['_'] = false;
			}
			break;
		}

		default:
			break;
	}
}

// D-Pad (hat) input mapping
static void handleJoystickHat(int hat)
{
	// Hat values: SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT, SDL_HAT_CENTERED
	// D-Pad could be used for hotbar selection or menu navigation

	if (hat == SDL_HAT_UP) {
		LOGI("D-Pad UP\n");
		// Could map to hotbar slot up or menu navigation
		Keyboard::feed(38, 1);  // UP arrow
		Keyboard::feed(38, 0);
	}
	else if (hat == SDL_HAT_DOWN) {
		LOGI("D-Pad DOWN\n");
		Keyboard::feed(40, 1);  // DOWN arrow
		Keyboard::feed(40, 0);
	}
	else if (hat == SDL_HAT_LEFT) {
		LOGI("D-Pad LEFT\n");
		Keyboard::feed(37, 1);  // LEFT arrow
		Keyboard::feed(37, 0);
	}
	else if (hat == SDL_HAT_RIGHT) {
		LOGI("D-Pad RIGHT\n");
		Keyboard::feed(39, 1);  // RIGHT arrow
		Keyboard::feed(39, 0);
	}
}

static int handleEvents()
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (SDL_QUIT == event.type) {
			return -1;
		}

		// Keyboard input
		if (SDL_KEYDOWN == event.type) {
			int key = event.key.keysym.sym;
			unsigned char transformed = transformKey(key);
			if (transformed) Keyboard::feed(transformed, 1);
		}
		if (SDL_KEYUP == event.type) {
			int key = event.key.keysym.sym;
			unsigned char transformed = transformKey(key);
			if (transformed) Keyboard::feed(transformed, 0);
		}

		// Mouse button input
		if (SDL_MOUSEBUTTONDOWN == event.type) {
			if (SDL_BUTTON_WHEELUP == event.button.button) {
				Mouse::feed(3, 0, event.button.x, event.button.y, 0, 1);
			} else if (SDL_BUTTON_WHEELDOWN == event.button.button) {
				Mouse::feed(3, 0, event.button.x, event.button.y, 0, -1);
			} else {
				bool left = SDL_BUTTON_LEFT == event.button.button;
				char button = left? 1 : 2;
				Mouse::feed(button, 1, event.button.x, event.button.y);
				Multitouch::feed(button, 1, event.button.x, event.button.y, 0);
			}
		}
		if (SDL_MOUSEBUTTONUP == event.type) {
			bool left = SDL_BUTTON_LEFT == event.button.button;
			char button = left? 1 : 2;
			Mouse::feed(button, 0, event.button.x, event.button.y);
			Multitouch::feed(button, 0, event.button.x, event.button.y, 0);
		}

		// Mouse motion
		if (SDL_MOUSEMOTION == event.type) {
			float x = event.motion.x;
			float y = event.motion.y;
			Multitouch::feed(0, 0, x, y, 0);
			Mouse::feed(0, 0, x, y, event.motion.xrel, event.motion.yrel);
		}

		// Joystick/Gamepad input
		if (SDL_JOYAXISMOTION == event.type) {
			int axis = event.jaxis.axis;
			float normalized = (float)event.jaxis.value / 32768.0f;
			handleJoystickAxis(axis, normalized);
		}

		if (SDL_JOYBUTTONDOWN == event.type) {
			handleJoystickButton(event.jbutton.button, 1);
		}

		if (SDL_JOYBUTTONUP == event.type) {
			handleJoystickButton(event.jbutton.button, 0);
		}

		if (SDL_JOYHATMOTION == event.type) {
			handleJoystickHat(event.jhat.value);
		}
	}
	return 0;
}

static void teardown()
{
	deinitEGL();
	if (g_joystick) {
		SDL_JoystickClose(g_joystick);
		g_joystick = NULL;
	}
	SDL_Quit();
}

int main(int argc, char** argv)
{
	// Initialize SDL with video and joystick support
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
		printf("Couldn't initialize SDL: %s\n", SDL_GetError());
		return -1;
	}

	SDL_Surface* sdlSurface = SDL_SetVideoMode(width, height, 32, SDL_SWSURFACE | SDL_RESIZABLE);
	if (sdlSurface == NULL) {
		printf("Couldn't create SDL window: %s\n", SDL_GetError());
		return -2;
	}

	// Open first available joystick (EmuELEC gamepad)
	if (SDL_NumJoysticks() > 0) {
		g_joystick = SDL_JoystickOpen(0);
		if (g_joystick) {
			printf("Gamepad found: %s\n", SDL_JoystickName(0));
			printf("  Buttons: %d\n", SDL_JoystickNumButtons(g_joystick));
			printf("  Axes: %d\n", SDL_JoystickNumAxes(g_joystick));
			printf("  Hats: %d\n", SDL_JoystickNumHats(g_joystick));
		} else {
			printf("Failed to open gamepad: %s\n", SDL_GetError());
		}
	} else {
		printf("No gamepad detected\n");
	}

	std::string path = argv[0];
	int e = path.rfind('/');
	if (e != std::string::npos) {
		path = path.substr(0, e);
		chdir(path.c_str());
	}

	char buf[1024];
	getcwd(buf, 1000);
	printf("Working directory: %s\n", buf);

	atexit(teardown);
	SDL_WM_SetCaption("Minecraft - Pi Edition (EmuELEC S905 Port)", 0);

	MAIN_CLASS* app = new MAIN_CLASS();
	std::string storagePath = getenv("HOME");
	storagePath += "/.minecraft/";
	app->externalStoragePath = storagePath;
	app->externalCacheStoragePath = storagePath;

	int commandPort = 0;
	if (argc > 1) {
		commandPort = atoi(argv[1]);
	}
	if (commandPort != 0) {
		app->commandPort = commandPort;
	}

	printf("Storage path: %s\n", app->externalStoragePath.c_str());

	AppContext context;
	AppPlatform_EmuELEC_S905 platform;
	context.doRender = true;
	context.platform = &platform;

	initEGL(app, &context, width, height);

	bool running = true;
	while (running) {
		running = handleEvents() == 0;
		app->update();
		eglSwapBuffers(g_display, g_surface);
	}

	deinitEGL();

	return 0;
}

#endif
