#pragma once


namespace input
{
    extern bool
		onLMBPress,
		onLMBRelease,
		LMBPressed,
		onRMBPress,
		onRMBRelease,
		RMBPressed;

	extern glm::ivec2 mouseDelta;

	extern bool
		keyUpPressed,
		keyDownPressed,
		keyLeftPressed,
		keyRightPressed,
		keyWorldUpPressed,
		keyWorldDownPressed,
		keyShiftPressed,
		keyDelPressed,
		keyCtrlPressed,
		keyQPressed,
		keyWPressed,
		keyEPressed,
		keyRPressed,
		keyDPressed,
		onKeyJumpPress,
		onKeyF10Press;

	void processInput(bool* isRunning, bool* isWindowMinimized);
}
