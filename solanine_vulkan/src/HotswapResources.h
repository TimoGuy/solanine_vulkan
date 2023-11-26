#pragma once
#ifdef _DEVELOP
#include <string>
#include <functional>
namespace std { class mutex; }
class VulkanEngine;
class RenderObjectManager;

namespace hotswapres
{
	void buildResourceList();
	std::mutex* startResourceChecker(VulkanEngine* engine, RenderObjectManager* roManager, bool* recreateSwapchain);
	void flagStopRunning();
	void waitForShutdownAndTeardownResourceList();

	struct ReloadCallback
	{
		void* owner;  // @NOTE: the `owner` void ptr is used to reference which callbacks need to be deleted
		std::function<void()> callback;
	};
	void addReloadCallback(const std::string& fname, void* owner, std::function<void()>&& reloadCallback);
	void removeOwnedCallbacks(void* owner);
}
#endif