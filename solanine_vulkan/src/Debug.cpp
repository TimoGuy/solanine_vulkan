#include "Debug.h"

#ifdef _DEVELOP

#include <glm/glm.hpp>
#include "imgui/imgui.h"


std::vector<debug::DebugMessage> debug::_debugMessages;

void debug::pushDebugMessage(const DebugMessage& message)
{
	_debugMessages.push_back(message);
}

void debug::renderImguiDebugMessages(const float_t& windowWidth, const float_t& deltaTime)
{
    static float_t debugMessagesWindowWidth = 0.0f;
	ImGui::SetNextWindowPos(ImVec2(windowWidth * 0.5f - debugMessagesWindowWidth * 0.5f, 0.0f), ImGuiCond_Always);
	ImGui::Begin("##Debug Messages", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
	{
		for (int32_t i = (int32_t)debug::_debugMessages.size() - 1; i >= 0; i--)
		{
			debug::DebugMessage& dm = debug::_debugMessages[(size_t)i];

			ImVec4 textColor(1, 1, 1, 1);
			switch (dm.type)
			{
			case 0:
				textColor = ImVec4(1, 1, 1, 1);
				break;

			case 1:
				textColor = ImVec4(1, 1, 0, 1);
				break;

			case 2:
				textColor = ImVec4(1, 0, 0, 1);
				break;
			}
			textColor.w = glm::clamp(dm.timeUntilDeletion / 0.35f, 0.0f, 1.0f);

			ImGui::TextColored(textColor, dm.message.c_str());
			dm.timeUntilDeletion -= deltaTime;

			if (dm.timeUntilDeletion <= 0)
				debug::_debugMessages.erase(debug::_debugMessages.begin() + (size_t)i);
		}

		debugMessagesWindowWidth = ImGui::GetWindowWidth();
	}
	ImGui::End();
}

#endif
