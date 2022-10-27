#include <glm/glm.hpp>

#include "InputManager.h"

#include <iostream>
#include <SDL2/SDL.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl.h"


extern bool input::onLMBPress = false;
extern bool input::onLMBRelease = false;
extern bool input::LMBPressed = false;
extern bool input::onRMBPress = false;
extern bool input::onRMBRelease = false;
extern bool input::RMBPressed = false;

extern glm::ivec2 input::mouseDelta = glm::ivec2(0);

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
extern bool input::onKeyJumpPress = false;
extern bool input::onKeyF10Press = false;


void input::processInput(bool* isRunning, bool* isWindowMinimized)
{
    input::onLMBPress = false;
    input::onLMBRelease = false;
    input::onRMBPress = false;
    input::onRMBRelease = false;
	input::onKeyJumpPress = false;
	input::onKeyF10Press = false;
	input::mouseDelta = { 0, 0 };

	SDL_Event e;
	while (SDL_PollEvent(&e) != 0)
	{
		ImGui_ImplSDL2_ProcessEvent(&e);

		switch (e.type)
		{
		case SDL_QUIT:
		{
			// Exit program
			*isRunning = false;
			break;
		}

		case SDL_MOUSEMOTION:
		{
			input::mouseDelta.x += e.motion.xrel;
			input::mouseDelta.y += e.motion.yrel;
			break;
		}

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		{
			switch (e.button.button)
			{
			case SDL_BUTTON_LEFT:
				input::onLMBPress = (e.button.type == SDL_MOUSEBUTTONDOWN);
				input::onLMBRelease = (e.button.type == SDL_MOUSEBUTTONUP);
				input::LMBPressed = (e.button.type == SDL_MOUSEBUTTONDOWN);
				break;

			case SDL_BUTTON_RIGHT:
				input::onRMBPress = (e.button.type == SDL_MOUSEBUTTONDOWN);
				input::onRMBRelease = (e.button.type == SDL_MOUSEBUTTONUP);
				input::RMBPressed = (e.button.type == SDL_MOUSEBUTTONDOWN);
				break;
			}
			break;
		}

		case SDL_KEYDOWN:
		case SDL_KEYUP:
		{
			if (e.key.keysym.sym == SDLK_w)                                           input::keyUpPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_s)                                           input::keyDownPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_a)                                           input::keyLeftPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_d)                                           input::keyRightPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_q)                                           input::keyWorldDownPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_e)                                           input::keyWorldUpPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_LSHIFT || e.key.keysym.sym == SDLK_RSHIFT)   input::keyShiftPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_DELETE)                                      input::keyDelPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_LCTRL || e.key.keysym.sym == SDLK_RCTRL)     input::keyCtrlPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_q)                                           input::keyQPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_w)                                           input::keyWPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_e)                                           input::keyEPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_r)                                           input::keyRPressed = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_SPACE)                                       input::onKeyJumpPress = (e.key.type == SDL_KEYDOWN);
			if (e.key.keysym.sym == SDLK_F10)                                         input::onKeyF10Press = (e.key.type == SDL_KEYDOWN);
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
                *isWindowMinimized = true;
                break;
            case SDL_WINDOWEVENT_MAXIMIZED:
                break;
            case SDL_WINDOWEVENT_RESTORED:
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
        }
	}
}
