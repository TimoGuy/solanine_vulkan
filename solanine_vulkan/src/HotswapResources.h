#pragma once
#ifdef _DEVELOP
namespace std { class mutex; }
class VulkanEngine;
class RenderObjectManager;

namespace hotswapres
{
	void buildResourceList();
	std::mutex* startResourceChecker(VulkanEngine* engine, bool* recreateSwapchain, RenderObjectManager* roManager);
	void shutdownAndTeardownResourceList();
}
#endif