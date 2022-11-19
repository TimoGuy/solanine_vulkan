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
	extern glm::ivec2  mouseScrollDelta;

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
		onKeyF10Press,
		onKeyF9Press,
		onKeyF8Press,
		onKeyLSBPress,
		onKeyRSBPress;

	void processInput(bool* isRunning, bool* isWindowMinimized);
}
