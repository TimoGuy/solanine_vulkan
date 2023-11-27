#include "pch.h"

#include "HotswapResources.h"

#ifdef _DEVELOP

#include "GLSLToSPIRVHelper.h"
#include "TextureCooker.h"
#include "MaterialOrganizer.h"
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
        { ".jpg", ".halfstep" },
        { ".png", ".halfstep" },
        { ".jpg", ".hrecipe" },
        { ".png", ".hrecipe" },
        { ".halfstep", ".hrecipe" },
        { ".hrecipe", ".hderriere" },
        { ".hderriere", "materialPropagation" },
        { ".vert", ".humba" },
        { ".frag", ".humba" },
        { ".humba", ".hderriere" },
        { ".humba", "rebuildPipelines" },
    };

    void checkIfResourceUpdatedThenHotswapRoutineAsync(VulkanEngine* engine, RenderObjectManager* roManager, bool* recreateSwapchain);
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
    
	std::mutex* startResourceChecker(VulkanEngine* engine, RenderObjectManager* roManager, bool* recreateSwapchain)
    {
        isAsyncRunnerRunning = true;
        isFirstRun = true;
        asyncRunner = new std::thread(checkIfResourceUpdatedThenHotswapRoutineAsync, engine, roManager, recreateSwapchain);
        while (isFirstRun) { }
        return &hotswapResourcesMutex;
    }

    struct CheckStageResource
    {
        bool includeInCheck;
        std::filesystem::path path;
    };

    inline bool executeHotswapOnResourcesThatNeedIt(VulkanEngine* engine, RenderObjectManager* roManager, const std::string& stageName, const std::vector<CheckStageResource>& resources, bool* recreateSwapchain)
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
        else if (stageName == ".humba")
        {
            for (auto& r : resources)
                if (r.includeInCheck &&
                    materialorganizer::checkMaterialBaseReloadNeeded(r.path) &&
                    materialorganizer::loadMaterialBase(r.path))
                    executedHotswap = true;
        }
        else if (stageName == ".hderriere")
        {
            for (auto& r : resources)
                if (r.includeInCheck &&
                    materialorganizer::checkDerivedMaterialParamReloadNeeded(r.path) &&
                    materialorganizer::loadDerivedMaterialParam(r.path))
                    executedHotswap = true;
        }
        else if (stageName == ".gltf" ||
                stageName == ".glb")
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
        else if (stageName == "rebuildPipelines")
        {
            // Trip reloading the shaders (recreate swapchain flag) @INCOMPLETE: there really should be a trigger into the materials system bc they should be the ones in charge of rebuilding the shaders (in .humba files) for the materials that got reloaded.
            *recreateSwapchain = true;
            executedHotswap = true;
        }
        else if (stageName == "materialPropagation")
        {
            // @TODO: propagate the materials!
            materialorganizer::cookTextureIndices();
            // executedHotswap = true;
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
                        executedHotswap = true;
                        continue;
                    }
                }
            }
        }

        return executedHotswap;
    }

    bool checkForCircularJobDependencyRecur(std::string myBefore, std::vector<std::string> markedBefores)
    {
        // Check for circular dependency.
        for (auto& before : markedBefores)
            if (myBefore == before)
            {
                std::cerr << "[CHECK RESOURCE CIRCULAR DEPENDENCIES]" << std::endl
                    << "ERROR: Circular dependency found: " << myBefore << std::endl;
                return true;  // Exit bc jobs cannot run with this set of dependencies.
            }

        // Recurse.
        markedBefores.push_back(myBefore);
        for (auto& otherJD : jobDependencies)
            if (myBefore == otherJD.before &&
                checkForCircularJobDependencyRecur(otherJD.after, markedBefores))
                return true;  // Bubble up the circular dependency.
        return false;
    }

	void checkIfResourceUpdatedThenHotswapRoutineAsync(VulkanEngine* engine, RenderObjectManager* roManager, bool* recreateSwapchain)
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

            // Check for any circular job dependencies.
            if (isFirstRun)
            {
                bool circularDependency = false;
                for (auto& jd : jobDependencies)     // @TODO: @RESUME @HERE: make this a recursive function since this needs to be able to fork.
                    if (checkForCircularJobDependencyRecur(jd.before, {}))
                    {
                        circularDependency = true;
                        break;
                    }                
            }

            // Short circuit if there are no jobs to check.
            if (!thereAreActuallyResourcesToCheck)
                continue;

            // Sort jobs into stages.
            struct JobStage
            {
                std::string stageName;
                std::vector<CheckStageResource> resources;
                std::vector<std::string> afters;
            };
            std::vector<JobStage> jobStages;

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

            // Connect dependencies of stages.
            for (auto& jobStage : jobStages)
                for (auto& depend : jobDependencies)
                    if (jobStage.stageName == depend.before)
                        jobStage.afters.push_back(depend.after);

            // Sort stages into order of dependencies.
            bool sorted = false;
            while (!sorted)
            {
                sorted = true;
                for (size_t b = 0; b < jobStages.size(); b++)
                {
                    auto& jobStageBefore = jobStages[b];
                    for (size_t a = 0; a < jobStages.size(); a++)
                    {
                        auto& jobStageAfter = jobStages[a];
                        for (auto& depend : jobDependencies)
                            if (depend.before == jobStageBefore.stageName &&
                                depend.after == jobStageAfter.stageName)
                            {
                                if (b == a)
                                    std::cerr << "You're an idiot.  -Dmitri" << std::endl;
                                else if (b > a)
                                {
                                    std::iter_swap(
                                        jobStages.begin() + b,
                                        jobStages.begin() + a
                                    );
                                    sorted = false;
                                }
                            }
                    }
                }
            }

            // Lock guard.
            {
                std::cout << "[RELOAD HOTSWAPPABLE RESOURCE]" << std::endl
                    << "Checking which resources to hotswap..." << std::endl;
                int32_t numGroupsProcessed = 0;
                std::lock_guard<std::mutex> lg(hotswapResourcesMutex);
                
                // Process each stage, traversing thru each linked list.
                for (JobStage& stage : jobStages)
                {
                    std::cout << "\tChecking " << stage.stageName << std::endl;
                    if (executeHotswapOnResourcesThatNeedIt(engine, roManager, stage.stageName, stage.resources, recreateSwapchain))
                    {
                        numGroupsProcessed++;
                        std::cout << "\t\tProcessed." << std::endl;
                        for (auto& after : stage.afters)
                            for (auto& otherStage : jobStages)
                                if (otherStage.stageName == after)
                                {
                                    for (auto& afterRes : otherStage.resources)  // Mark all resources in the after job stage as ones to check.
                                        afterRes.includeInCheck = true;
                                    break;  // Should only be one job stage with a certain name and we found it.
                                }
                    }
                }

                if (numGroupsProcessed == 0)
                    std::cout << "None Processed." << std::endl;
                else
                    std::cout << numGroupsProcessed << " Groups Processed." << std::endl;
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