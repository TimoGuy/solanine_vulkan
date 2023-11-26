#pragma once
#ifdef _DEVELOP


namespace debug
{
    struct DebugMessage
	{
		std::string message;
		uint32_t type = 0;  // 0: info | 1: warning | 2: error
		float_t timeUntilDeletion = 5.0f;  // @NOTE: use this to lengthen certain messages, like error ones
	};

    extern std::vector<DebugMessage> _debugMessages;

	void pushDebugMessage(const DebugMessage& message);
	void renderImguiDebugMessages(const float_t& windowWidth, float_t deltaTime);
}
#endif
