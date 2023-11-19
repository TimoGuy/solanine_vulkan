#include "ImportGLM.h"

#include "InputManager.h"

#include <iostream>
#include <SDL2/SDL.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl.h"

#ifdef _DEVELOP
#include <map>
#include <thread>
#endif

#ifdef _DEBUG
#include <mutex>
#endif

namespace input
{
	struct KeyboardMouseInputState
	{
		vec2 mousePosition = GLM_VEC2_ZERO_INIT;
		vec2 mouseDelta = GLM_VEC2_ZERO_INIT;
		vec2 mouseScrollDelta = GLM_VEC2_ZERO_INIT;
		bool LMB = false;
		bool RMB = false;
		bool mouseMoved = false;

		bool F1 = false;
		bool F2 = false;
		bool F3 = false;
		bool F11 = false;
		bool del = false;
		bool lCtrl = false;
		bool lShift = false;
		bool q = false;
		bool e = false;
		bool w = false;
		bool a = false;
		bool s = false;
		bool d = false;
		bool f = false;
		bool r = false;
		bool c = false;
		bool x = false;
		bool v = false;
		bool tab = false;
		bool esc = false;
		bool space = false;
		bool lSqrBracket = false;
		bool rSqrBracket = false;
		bool enter = false;
	} keyMouseState;

	void processInput(bool* isRunning, bool* isWindowMinimized)
	{
		// Reset deltas.
		glm_vec2_zero(keyMouseState.mouseDelta);
		glm_vec2_zero(keyMouseState.mouseScrollDelta);
		keyMouseState.mouseMoved = false;

		SDL_Event e;
		while (SDL_PollEvent(&e) != 0)
		{
			ImGui_ImplSDL2_ProcessEvent(&e);

			switch (e.type)
			{
			case SDL_MOUSEMOTION:
			{
				keyMouseState.mouseDelta[0] += e.motion.xrel;
				keyMouseState.mouseDelta[1] += e.motion.yrel;
				keyMouseState.mouseMoved = true;
				break;
			}

			case SDL_MOUSEWHEEL:
			{
				keyMouseState.mouseScrollDelta[0] += e.wheel.x;
				keyMouseState.mouseScrollDelta[1] += e.wheel.y;
				break;
			}

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
			{
				switch (e.button.button)
				{
				case SDL_BUTTON_LEFT:
					keyMouseState.LMB = (e.button.type == SDL_MOUSEBUTTONDOWN);
					break;

				case SDL_BUTTON_RIGHT:
					keyMouseState.RMB = (e.button.type == SDL_MOUSEBUTTONDOWN);
					break;
				}
				break;
			}

			case SDL_KEYDOWN:
			case SDL_KEYUP:
			{
				if (e.key.repeat)
					break;  // @NOTE: ignore key repeats (i.e. when you hold a key down and it repeats the character) (e.g. aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa)

				bool pressed = (e.key.type == SDL_KEYDOWN);
				switch (e.key.keysym.sym)
				{
					case SDLK_F1:                         keyMouseState.F1 = pressed; break;
					case SDLK_F2:                         keyMouseState.F2 = pressed; break;
					case SDLK_F3:                         keyMouseState.F3 = pressed; break;
					case SDLK_F11:                        keyMouseState.F11 = pressed; break;
					case SDLK_DELETE:                     keyMouseState.del = pressed; break;
					case SDLK_LCTRL:                      keyMouseState.lCtrl = pressed; break;
					case SDLK_LSHIFT:                     keyMouseState.lShift = pressed; break;
					case SDLK_q:                          keyMouseState.q = pressed; break;
					case SDLK_e:                          keyMouseState.e = pressed; break;
					case SDLK_w:                          keyMouseState.w = pressed; break;
					case SDLK_a:                          keyMouseState.a = pressed; break;
					case SDLK_s:                          keyMouseState.s = pressed; break;
					case SDLK_d:                          keyMouseState.d = pressed; break;
					case SDLK_f:                          keyMouseState.f = pressed; break;
					case SDLK_r:                          keyMouseState.r = pressed; break;
					case SDLK_c:                          keyMouseState.c = pressed; break;
					case SDLK_x:                          keyMouseState.x = pressed; break;
					case SDLK_v:                          keyMouseState.v = pressed; break;
					case SDLK_TAB:                        keyMouseState.tab = pressed; break;
					case SDLK_ESCAPE:                     keyMouseState.esc = pressed; break;
					case SDLK_SPACE:                      keyMouseState.space = pressed; break;
					case SDLK_LEFTBRACKET:                keyMouseState.lSqrBracket = pressed; break;
					case SDLK_RIGHTBRACKET:               keyMouseState.rSqrBracket = pressed; break;
					case SDLK_RETURN:                     keyMouseState.enter = pressed; break;
				}
				break;
			}

			case SDL_WINDOWEVENT:
			{
				switch (e.window.event)
				{
				case SDL_WINDOWEVENT_SHOWN:
					break;
				case SDL_WINDOWEVENT_HIDDEN:
					break;
				case SDL_WINDOWEVENT_EXPOSED:
					break;
				case SDL_WINDOWEVENT_MOVED:
					break;
				case SDL_WINDOWEVENT_RESIZED:
					break;
				case SDL_WINDOWEVENT_SIZE_CHANGED:
					break;
				case SDL_WINDOWEVENT_MINIMIZED:
					// Inform window is now minimized.
					//
					// @NOTE: Vulkan rendering is not able to present a render unless if the window is restored.
					//        Bc of this, if an attempt to render to a swapchain buffer is made, the GPU will be reported
					//        as missing, causing a program crash. Hence this flag informing when to not attempt a render.
					//            -Timo 2023/11/18
					*isWindowMinimized = true;
					break;
				case SDL_WINDOWEVENT_MAXIMIZED:
					break;
				case SDL_WINDOWEVENT_RESTORED:
					// Inform window is now restored.
					*isWindowMinimized = false;
					break;
				case SDL_WINDOWEVENT_ENTER:
					break;
				case SDL_WINDOWEVENT_LEAVE:
					break;
				case SDL_WINDOWEVENT_FOCUS_GAINED:
					break;
				case SDL_WINDOWEVENT_FOCUS_LOST:
					break;
				case SDL_WINDOWEVENT_CLOSE:
					break;
	#if SDL_VERSION_ATLEAST(2, 0, 5)
				case SDL_WINDOWEVENT_TAKE_FOCUS:
					break;
				case SDL_WINDOWEVENT_HIT_TEST:
					break;
	#endif
				default:
					break;
				}
				break;
			}
			
			case SDL_QUIT:
			{
				// Exit program
				*isRunning = false;
				break;
			}
			}
		}
	}

	// Input Sets.
	void EditorInputSet::update()
	{
		togglePlayEditMode.update(keyMouseState.F1);
		toggleEditorUI.update(keyMouseState.F2);
		cycleRenderingModes.update(keyMouseState.F3);
		cancel.update(keyMouseState.esc);
		submit.update(keyMouseState.enter);
		toggleTransformManipulationMode.update(keyMouseState.q);
		switchToTransformPosition.update(keyMouseState.w);
		switchToTransformRotation.update(keyMouseState.e);
		switchToTransformScale.update(keyMouseState.r);
		halveTimescale.update(keyMouseState.lSqrBracket);
		doubleTimescale.update(keyMouseState.rSqrBracket);
		pickObject.update(keyMouseState.LMB);
		deleteObject.update(keyMouseState.del);
		duplicateObject.update(keyMouseState.lCtrl && keyMouseState.d);
		actionC.update(keyMouseState.c);
		actionX.update(keyMouseState.x);
		actionV.update(keyMouseState.v);
		snapModifier.update(keyMouseState.lCtrl);
		backwardsModifier.update(keyMouseState.lShift);

		freeCamMode.update(keyMouseState.RMB);

		float_t h = 0.0f, v = 0.0f;
		h += keyMouseState.a ? -1.0f : 0.0f;
		h += keyMouseState.d ?  1.0f : 0.0f;
		v += keyMouseState.w ?  1.0f : 0.0f;
		v += keyMouseState.s ? -1.0f : 0.0f;
		freeCamMovement.update(h, v);

		float_t w = 0.0f;
		if (keyMouseState.q)
			w += -1.0f;
		if (keyMouseState.e)
			w += 1.0f;
		verticalFreeCamMovement.update(w);
		fastCameraMovement.update(keyMouseState.lShift);
	}

	std::map<std::thread::id, EditorInputSet> threadToEditorInputSet;

	void registerEditorInputSetOnThisThread()
	{
		threadToEditorInputSet[std::this_thread::get_id()] = EditorInputSet{};
	}

	EditorInputSet& editorInputSet()
	{
		return threadToEditorInputSet[std::this_thread::get_id()];
	}

	void RenderThreadInputSet::update(float_t deltaTime)
	{
		UIGoLeft.update(keyMouseState.a);
		UIGoRight.update(keyMouseState.d);
		UIGoUp.update(keyMouseState.w);
		UIGoDown.update(keyMouseState.s);
		UIConfirm.update(keyMouseState.LMB || keyMouseState.space);
		UICancel.update(keyMouseState.RMB || keyMouseState.esc);
		UICursorPosition.update(keyMouseState.mousePosition[0], keyMouseState.mousePosition[1]);
		UICursorPositionUpdate.update(keyMouseState.mouseMoved);
		UIScrollDelta.update(keyMouseState.mouseScrollDelta[0], keyMouseState.mouseScrollDelta[1]);
		cameraDelta.update(keyMouseState.mouseDelta[0], keyMouseState.mouseDelta[1]);  // Use `deltaTime` right here for joystick input.
		toggleInventory.update(keyMouseState.tab);
		togglePause.update(keyMouseState.esc);
		toggleTransformMenu.update(keyMouseState.f);
		toggleFullscreen.update(keyMouseState.F11);
	}
	
	RenderThreadInputSet& renderInputSet()
	{
		static RenderThreadInputSet instance = {};
#ifdef _DEBUG
		// Assert that the same thread is using this instance of input set.
		static bool registeredInstance = false;
		static std::mutex registerInstanceMutex;
		static std::thread::id masterThreadId;
		if (registeredInstance)
		{
			assert(std::this_thread::get_id() == masterThreadId);
		}
		else
		{
			std::lock_guard<std::mutex> lg(registerInstanceMutex);
			if (!registeredInstance)  // Still need to check to prevent overwrite from another process that was waiting.
			{
				masterThreadId = std::this_thread::get_id();
				registeredInstance = true;
			}
		}
#endif
		return instance;
	}
	
	void SimulationThreadInputSet::update()
	{
		float_t h = 0.0f, v = 0.0f;
		h += keyMouseState.a ? -1.0f : 0.0f;
		h += keyMouseState.d ?  1.0f : 0.0f;
		v += keyMouseState.w ?  1.0f : 0.0f;
		v += keyMouseState.s ? -1.0f : 0.0f;
		flatPlaneMovement.update(h, v);
		jump.update(keyMouseState.space);
		attack.update(keyMouseState.LMB);
		detach.update(keyMouseState.RMB);
		focus.update(keyMouseState.lShift);
		interact.update(keyMouseState.e);
	}

	SimulationThreadInputSet& simInputSet()
	{
		static SimulationThreadInputSet instance = {};
#ifdef _DEBUG
		// Assert that the same thread is using this instance of input set.
		static bool registeredInstance = false;
		static std::mutex registerInstanceMutex;
		static std::thread::id masterThreadId;
		if (registeredInstance)
		{
			assert(std::this_thread::get_id() == masterThreadId);
		}
		else
		{
			std::lock_guard<std::mutex> lg(registerInstanceMutex);
			if (!registeredInstance)  // Still need to check to prevent overwrite from another process that was waiting.
			{
				masterThreadId = std::this_thread::get_id();
				registeredInstance = true;
			}
		}
#endif
		return instance;
	}
}


#if 0
extern bool input::onLMBPress = false;
extern bool input::onLMBRelease = false;
extern bool input::LMBPressed = false;
extern bool input::onRMBPress = false;
extern bool input::onRMBRelease = false;
extern bool input::RMBPressed = false;

extern ivec2 input::mouseDelta = { 0, 0 };
extern ivec2 input::mouseScrollDelta = { 0, 0 };

extern bool input::keyUpPressed = false;
extern bool input::keyDownPressed = false;
extern bool input::keyLeftPressed = false;
extern bool input::keyRightPressed = false;
extern bool input::keyWorldUpPressed = false;
extern bool input::keyWorldDownPressed = false;
extern bool input::keyShiftPressed = false;
extern bool input::keyDelPressed = false;
extern bool input::keyCtrlPressed = false;
extern bool input::keyQPressed = false;
extern bool input::keyWPressed = false;
extern bool input::keyEPressed = false;
extern bool input::keyRPressed = false;
extern bool input::keyDPressed = false;
extern bool input::keyCPressed = false;
extern bool input::keyXPressed = false;
extern bool input::keyVPressed = false;
extern bool input::keyEscPressed = false;
extern bool input::keyEnterPressed = false;
extern bool input::keyTargetPressed = false;
extern bool input::onKeyJumpPress = false;
extern bool input::keyJumpPressed = false;
extern bool input::onKeyInteractPress = false;
extern bool input::onKeyF11Press = false;
extern bool input::onKeyF10Press = false;
extern bool input::onKeyF9Press = false;
extern bool input::onKeyF8Press = false;
extern bool input::onKeyLSBPress = false;
extern bool input::onKeyRSBPress = false;
extern bool input::onKeyF1Press = false;
#endif
