#ifdef _DEVELOP
#include "HotswapResources.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <thread>
#include <SDL2/SDL.h>
#include "GLSLToSPIRVHelper.h"
#include "TextureCooker.h"
#include "RenderObject.h"


namespace hotswapres
{
    struct ResourceToWatch
	{
        bool stale;
		std::filesystem::path           path;
		std::filesystem::file_time_type lastWriteTime;
	};
	std::vector<ResourceToWatch> resourcesToWatch;

    std::vector<std::string> ignoreExtensions = {
        ".spv",
        ".log",
        ".swp",
        ".gitkeep",
    };

    struct JobDependency
    {
        std::string before, after;
    };
    std::vector<JobDependency> jobDependencies = {
        { ".jpg", ".hrecipe" },  // @NOTE: can't have .jpg/.png both be dependants of .halfstep and .hrecipe, so just doing .hrecipe would probs be better. This is due to the limitation of the hotswap dependency system.  -Timo 2023/11/25
        { ".png", ".hrecipe" },
        { ".halfstep", ".hrecipe" },
        { ".hrecipe", ".hderriere" },
        { ".hderriere", "materialPropagation" },
        { ".vert", ".humba" },
        { ".frag", ".humba" },
        { ".humba", "rebuildPipelines" },
    };

    void checkIfResourceUpdatedThenHotswapRoutineAsync(VulkanEngine* engine, bool* recreateSwapchain, RenderObjectManager* roManager);
    bool isAsyncRunnerRunning;
    bool isFirstRun;
    std::thread* asyncRunner = nullptr;
	std::mutex hotswapResourcesMutex;

	void buildResourceList()
    {
        return;

        for (const auto& entry : std::filesystem::recursive_directory_iterator("res"))
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
                .stale = false,
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

            // Cook texture if corresponding cooked texture isn't up to date.
            if (ext.compare(".halfstep") == 0)
            {
                if (texturecooker::checkHalfStepNeeded(resource.path))
                    texturecooker::cookHalfStepFromRecipe(resource.path);
            }

            // Cook texture if corresponding cooked texture isn't up to date.
            if (ext.compare(".hrecipe") == 0)
            {
                if (texturecooker::checkTextureCookNeeded(resource.path))
                    texturecooker::cookTextureFromRecipe(resource.path);
            }
        }
    }

    std::unordered_map<std::string, std::vector<ReloadCallback>> resourceReloadCallbackMap;
    
	std::mutex* startResourceChecker(VulkanEngine* engine, bool* recreateSwapchain, RenderObjectManager* roManager)
    {
        isAsyncRunnerRunning = true;
        isFirstRun = true;
        asyncRunner = new std::thread(checkIfResourceUpdatedThenHotswapRoutineAsync, engine, recreateSwapchain, roManager);
        while (isFirstRun) { }
        return &hotswapResourcesMutex;
    }

    struct CheckStageResource
    {
        bool includeInCheck;
        std::filesystem::path path;
    };

    inline bool executeHotswapOnResourcesThatNeedIt(VulkanEngine* engine, RenderObjectManager* roManager, const std::string& stageName, const std::vector<CheckStageResource>& resources)
    {
        bool executedHotswap = false;
        if (stageName == ".jpg" ||
            stageName == ".png")
        {
            // Just force further dependencies to check on the images.
            executedHotswap = true;
        }
        else if (stageName == ".halfstep")
        {
            for (auto& r : resources)
                if (r.includeInCheck &&
                    texturecooker::checkHalfStepNeeded(r.path) &&
                    texturecooker::cookHalfStepFromRecipe(r.path))
                    executedHotswap = true;
        }
        else if (stageName == ".hrecipe")
        {
            for (auto& r : resources)
                if (r.includeInCheck &&
                    texturecooker::checkTextureCookNeeded(r.path) &&
                    texturecooker::cookTextureFromRecipe(r.path))
                    executedHotswap = true;
        }
        else if (stageName == ".vert" ||
            stageName == ".frag" ||
            stageName == ".comp")
        {
            for (auto& r : resources)
                if (r.includeInCheck &&
                    glslToSPIRVHelper::checkGLSLShaderCompileNeeded(r.path) &&
                    glslToSPIRVHelper::compileGLSLShaderToSPIRV(r.path))
                    executedHotswap = true;
        }
        else if (stageName ==".gltf" ||
                stageName ==".glb")
        {
            for (auto& r : resources)
            {
                if (!r.includeInCheck)
                    continue;

                roManager->reloadModelAndTriggerCallbacks(engine, r.path.stem().string(), r.path.string());
                std::cout << "Sent message to model \"" << r.path.stem().string() << "\" to reload." << std::endl;
                executedHotswap = true;
            }
        }
        else
        {
            // Execute all callback functions attached to resource name.
            for (auto& r : resources)
            {
                if (!r.includeInCheck)
                    continue;

                std::string fname = r.path.string();
                auto it = resourceReloadCallbackMap.find(fname);
                if (it != resourceReloadCallbackMap.end())
                {
                    for (auto& rc : resourceReloadCallbackMap[fname])
                        rc.callback();

                    size_t callbacks = resourceReloadCallbackMap[fname].size();
                    if (callbacks > 0)
                    {
                        std::cout << "Executed " << callbacks << " callback function(s) for \"" << fname << "\" to reload." << std::endl;
                        continue;
                    }
                }
            }
        }

        return executedHotswap;
    }

	void checkIfResourceUpdatedThenHotswapRoutineAsync(VulkanEngine* engine, bool* recreateSwapchain, RenderObjectManager* roManager)
    {
        while (isAsyncRunnerRunning)
        {
            for (auto& resource : resourcesToWatch)
                resource.stale = true;

            // Check for new or changed resources.
            size_t nextRTWSearchIdx = 0;
            std::vector<ResourceToWatch> newResources;
            std::vector<CheckStageResource> resourcesToCheck;
            bool thereAreActuallyResourcesToCheck = false;

            for (const auto& entry : std::filesystem::recursive_directory_iterator("res"))
            {
                // Ignore resource depending on circumstance.
                const auto& path = entry.path();

                if (std::filesystem::is_directory(path))
                    continue;		// Ignore directories

                if (!path.has_extension())
                    continue;		// @NOTE: only allow resource files if they have an extension!  -Timo

                {
                    bool ignore = false;
                    for (auto& ext : ignoreExtensions)
                        if (path.extension().compare(ext) == 0)
                        {
                            ignore = true;
                            break;
                        }
                    if (ignore)
                        continue;  // Ignore extensions.
                }

                // Match current entry to an RTW (resource to watch).
                ResourceToWatch* rtw = nullptr;
                if (!resourcesToWatch.empty())
                {
                    size_t i = nextRTWSearchIdx;
                    do
                    {
                        auto& currentRTW = resourcesToWatch[i];
                        if (!currentRTW.stale)
                            continue;  // Skip search if already processed (not stale).
                        if (currentRTW.path == path)
                        {
                            // Found the RTW.
                            rtw = &currentRTW;
                            nextRTWSearchIdx = (i + 1) % resourcesToWatch.size();
                            break;
                        }

                        i = (i + 1) % resourcesToWatch.size();
                    } while (i != nextRTWSearchIdx);
                }

                // Process RTW.
                if (rtw == nullptr)
                {
                    // Create new.
                    newResources.push_back({
                        .path = path,
                        .lastWriteTime = std::filesystem::last_write_time(path),
                    });
                    resourcesToCheck.push_back({
                        .includeInCheck = true,
                        .path = path,
                    });
                    thereAreActuallyResourcesToCheck = true;
                }
                else
                {
                    // See if RTW changed.
                    rtw->stale = false;
                    bool includeInCheck = false;
                    const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(rtw->path);
                    if (rtw->lastWriteTime != lastWriteTime)
                    {
                        includeInCheck = true;
                        thereAreActuallyResourcesToCheck = true;
                        rtw->lastWriteTime = lastWriteTime;
                    }
                    resourcesToCheck.push_back({
                        .includeInCheck = includeInCheck,
                        .path = rtw->path,
                    });
                }
            }

            // Remove stale resources.
            std::erase_if(
                resourcesToWatch,
                [](ResourceToWatch& rtw) {
                    return rtw.stale;
                }
            );

            // Add new resources.
            for (auto& nr : newResources)
                resourcesToWatch.push_back(nr);

            // Check all resources for first time check.
            if (isFirstRun)
            {
                for (auto& rtc : resourcesToCheck)
                    rtc.includeInCheck = true;
            }

            // Short circuit if there are no jobs to check.
            if (!thereAreActuallyResourcesToCheck)
                continue;

            // Sort jobs into stages.
            struct JobStageNode
            {
                std::string stageName;
                std::vector<CheckStageResource> resources;
                bool isHead = true;
                JobStageNode* next = nullptr;
            };
            std::vector<JobStageNode> jobStages;

            for (auto& rth : resourcesToCheck)
            {
                std::string stageName = rth.path.extension().string();

                // Bucket job into a stage.
                bool found = false;
                for (auto& stage : jobStages)
                    if (stage.stageName == stageName)  // Insert into existing bucket.
                    {
                        stage.resources.push_back(rth);
                        found = true;
                        break;
                    }
                if (!found)  // New bucket needed.
                    jobStages.push_back({
                        .stageName = stageName,
                        .resources = { rth },
                    });
            }

            // Connect stages into a linked list.
            for (auto& depend : jobDependencies)
                for (auto& stageBefore : jobStages)
                    if (stageBefore.stageName == depend.before)
                    {
                        for (auto& stageAfter : jobStages)
                            if (stageAfter.stageName == depend.after)
                            {
                                stageBefore.next = &stageAfter;
                                stageAfter.isHead = false;
                                break;
                            }
                        break;
                    }

            std::vector<JobStageNode*> jobStagesHeadRefs;
            for (auto& stage : jobStages)
                if (stage.isHead)
                    jobStagesHeadRefs.push_back(&stage);

            // Lock guard.
            {
                std::cout << "[RELOAD HOTSWAPPABLE RESOURCE]" << std::endl
                    << "Checking which resources to hotswap..." << std::endl;
                std::lock_guard<std::mutex> lg(hotswapResourcesMutex);
                
                // Process each stage, traversing thru each linked list.
                for (JobStageNode* headRef : jobStagesHeadRefs)
                {
                    JobStageNode* n = headRef;
                    while (n != nullptr)
                    {
                        if (executeHotswapOnResourcesThatNeedIt(engine, roManager, n->stageName, headRef->resources) &&
                            n->next != nullptr)
                            for (auto& nextRes : n->next->resources)  // Mark all resources in the next node as check.
                                nextRes.includeInCheck = true;
                        n = n->next;
                    }
                }
            }


#if 0
                    continue;

                // Insert lock guard to reload resources
                std::cout << "[RELOAD HOTSWAPPABLE RESOURCE]" << std::endl
                    << "Asking to swap resources..." << std::endl;
                std::lock_guard<std::mutex> lg(hotswapResourcesMutex);

                //
                // Reload the resource
                //
                std::cout << "[RELOAD HOTSWAPPABLE RESOURCE]" << std::endl
                    << "Name: " << rtw->path << std::endl;
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
                    ext.compare(".frag") == 0 ||
                    ext.compare(".comp") == 0)
                {
                    // Compile the shader (GLSL -> SPIRV)
                    glslToSPIRVHelper::compileGLSLShaderToSPIRV(resource.path);

                    // Trip reloading the shaders (recreate swapchain flag)
                    *recreateSwapchain = true;
                    std::cout << "Recompile shader to SPIRV and trigger swapchain recreation SUCCESS" << std::endl;
                    continue;
                }
                else if (ext.compare(".hrecipe"))
                {
                    // Cook textures
                    texturecooker::cookTextureFromRecipe(resource.path);
                }
                else if (ext.compare(".gltf") == 0 ||
                        ext.compare(".glb")  == 0)
                {
                    roManager->reloadModelAndTriggerCallbacks(engine, resource.path.stem().string(), resource.path.string());
                    std::cout << "Sent message to model \"" << resource.path.stem().string() << "\" to reload." << std::endl;
                    continue;
                }
                else
                {
                    // Execute all callback functions attached to resource name.
                    std::string fname = resource.path.string();
                    auto it = resourceReloadCallbackMap.find(fname);
                    if (it != resourceReloadCallbackMap.end())
                    {
                        for (auto& rc : resourceReloadCallbackMap[fname])
                            rc.callback();

                        size_t callbacks = resourceReloadCallbackMap[fname].size();
                        if (callbacks > 0)
                        {
                            std::cout << "Executed " << callbacks << " callback function(s) for \"" << fname << "\" to reload." << std::endl;
                            continue;
                        }
                    }
                }
#endif

            // Just a simple static delay of 1 sec to not overload the filesystem.
            isFirstRun = false;
            SDL_Delay(1000);
        }
    }

    void flagStopRunning()
    {
        isAsyncRunnerRunning = false;
    }

	void waitForShutdownAndTeardownResourceList()
    {
        //
        // Shutdown the thread
        //
        isAsyncRunnerRunning = false;  // Redundant just in case
        asyncRunner->join();
        delete asyncRunner;

        // @NOTE: nothing is around to tear down!
        //        There are just filesystem entries, so they only go until the lifetime
        //        of the VulkanEngine object, so we don't need to tear that down!
    }

    void addReloadCallback(const std::string& fname, void* owner, std::function<void()>&& reloadCallback)
    {
        ReloadCallback rc = {
            .owner = owner,
            .callback = reloadCallback,
        };
        std::string fnamePathified =
            std::filesystem::path(fname)
                .make_preferred()  // Changes slashes to what the os preferred slashing style (i.e. '/' or '\\') is.
                .string();
        resourceReloadCallbackMap[fnamePathified].push_back(rc);
    }

    void removeOwnedCallbacks(void* owner)
    {
        for (auto it = resourceReloadCallbackMap.begin(); it != resourceReloadCallbackMap.end(); it++)
        {
            auto& rcs = it->second;
            std::erase_if(
                rcs,
                [owner](ReloadCallback x) {
                    return x.owner == owner;
                }
            );
        }
    }
}
#endif