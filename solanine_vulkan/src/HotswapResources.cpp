#ifdef _DEVELOP
#include "HotswapResources.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <thread>
#include <SDL2/SDL.h>
#include "GLSLToSPIRVHelper.h"
#include "RenderObject.h"


namespace hotswapres
{
    struct ResourceToWatch
	{
		std::filesystem::path           path;
		std::filesystem::file_time_type lastWriteTime;
	};
	std::vector<ResourceToWatch> resourcesToWatch;

    void checkIfResourceUpdatedThenHotswapRoutineAsync(VulkanEngine* engine, bool* recreateSwapchain, RenderObjectManager* roManager);
    bool isAsyncRunnerRunning;
    std::thread* asyncRunner = nullptr;
	std::mutex hotswapResourcesMutex;

	void buildResourceList()
    {
        std::vector<std::string> directories = {
            "res",
            "shader",
        };
        for (auto directory : directories)
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
            {
                // Add the resource if it should be watched
                const auto& path = entry.path();
                if (std::filesystem::is_directory(path))
                    continue;		// Ignore directories
                if (!path.has_extension())
                    continue;		// @NOTE: only allow resource files if they have an extension!  -Timo

                if (path.extension().compare(".spv") == 0 ||
                    path.extension().compare(".log") == 0)
                    continue;		// @NOTE: ignore compiled SPIRV shader files, logs

                ResourceToWatch resource = {
                    .path = path,
                    .lastWriteTime = std::filesystem::last_write_time(path),
                };
                resourcesToWatch.push_back(resource);

                // Compile glsl shader if corresponding .spv file isn't up to date
                const auto& ext = path.extension();
                if (ext.compare(".vert") == 0 ||
                    ext.compare(".frag") == 0)		// @COPYPASTA
                {
                    auto spvPath = path;
                    spvPath += ".spv";

                    if (!std::filesystem::exists(spvPath) ||
                        std::filesystem::last_write_time(spvPath) <= std::filesystem::last_write_time(path))
                    {
                        glslToSPIRVHelper::compileGLSLShaderToSPIRV(path);
                    }
                }
            }
    }
    
	std::mutex* startResourceChecker(VulkanEngine* engine, bool* recreateSwapchain, RenderObjectManager* roManager)
    {
        isAsyncRunnerRunning = true;
        asyncRunner = new std::thread(checkIfResourceUpdatedThenHotswapRoutineAsync, engine, recreateSwapchain, roManager);
        return &hotswapResourcesMutex;
    }

	void checkIfResourceUpdatedThenHotswapRoutineAsync(VulkanEngine* engine, bool* recreateSwapchain, RenderObjectManager* roManager)
    {
        while (isAsyncRunnerRunning)
        {
            for (auto& resource : resourcesToWatch)
            {
                /*try
                {*/
                    const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(resource.path);
                    if (resource.lastWriteTime == lastWriteTime)
                        continue;

                    // Insert lock guard to reload resources
                    std::cout << "[RELOAD HOTSWAPPABLE RESOURCE]" << std::endl
                        << "Asking to swap resources..." << std::endl;
                    std::lock_guard<std::mutex> lg(hotswapResourcesMutex);

                    //
                    // Reload the resource
                    //
                    std::cout << "[RELOAD HOTSWAPPABLE RESOURCE]" << std::endl
                        << "Name: " << resource.path << std::endl;
                    resource.lastWriteTime = lastWriteTime;

                    if (!resource.path.has_extension())
                    {
                        std::cerr << "ERROR: file " << resource.path << " has no extension!" << std::endl;
                        continue;
                    }

                    //
                    // Find the extension and execute appropriate routine
                    //
                    const auto& ext = resource.path.extension();
                    if (ext.compare(".vert") == 0 ||
                        ext.compare(".frag") == 0)
                    {

                        // Compile the shader (GLSL -> SPIRV)
                        glslToSPIRVHelper::compileGLSLShaderToSPIRV(resource.path);

                        // Trip reloading the shaders (recreate swapchain flag)
                        *recreateSwapchain = true;
                        std::cout << "Recompile shader to SPIRV and trigger swapchain recreation SUCCESS" << std::endl;
                        continue;
                    }
                    else if (ext.compare(".gltf") == 0 ||
                            ext.compare(".glb")  == 0)
                    {
                        roManager->reloadModelAndTriggerCallbacks(engine, resource.path.stem().string(), resource.path.string());
                        std::cout << "Sent message to model \"" << resource.path.stem().string() << "\" to reload." << std::endl;
                        continue;
                    }

                    // Nothing to do to the resource!
                    // That means there's no routine for this certain resource
                    std::cout << "WARNING: No routine for " << ext << " files!" << std::endl;
                /*}
                catch (...) { }*/   // Just continue on if you get the filesystem error
            }

            // Just a simple static delay of 1 sec
            SDL_Delay(1000);
        }
    }

	void shutdownAndTeardownResourceList()
    {
        //
        // Shutdown the thread
        //
        isAsyncRunnerRunning = false;
        asyncRunner->join();
        delete asyncRunner;

        // @NOTE: nothing is around to tear down!
        //        There are just filesystem entries, so they only go until the lifetime
        //        of the VulkanEngine object, so we don't need to tear that down!
    }
}
#endif