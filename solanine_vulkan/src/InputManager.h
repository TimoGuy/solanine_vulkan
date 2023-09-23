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

	extern ivec2 mouseDelta;
	extern ivec2  mouseScrollDelta;

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
		keyCPressed,
		keyXPressed,
		keyEscPressed,
		keyEnterPressed,
		keyTargetPressed,
		onKeyJumpPress,
		keyJumpPressed,
		onKeyInteractPress,
		onKeyF10Press,
		onKeyF9Press,
		onKeyF8Press,
		onKeyLSBPress,
		onKeyRSBPress,
		onKeyF1Press;

	void processInput(bool* isRunning, bool* isWindowMinimized);
}
