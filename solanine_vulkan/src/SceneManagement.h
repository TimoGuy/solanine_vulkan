#pragma once

#include <string>
#include <vector>
class VulkanEngine;
class Entity;
class DataSerialized;


namespace scene
{
    std::vector<std::string> getListOfEntityTypes();
    Entity* spinupNewObject(const std::string& objectName, VulkanEngine* engine, DataSerialized* ds);

    const std::string SCENE_DIRECTORY_PATH = "res/scenes/";
    const std::string PREFAB_DIRECTORY_PATH = "res/prefabs/";
    std::vector<std::string> getListOfScenes();
    std::vector<std::string> getListOfPrefabs();
    bool loadPrefab(const std::string& name, VulkanEngine* engine, std::vector<Entity*>& outEntityPtrs);
    bool loadPrefabNonOwned(const std::string& name, VulkanEngine* engine);
    bool loadScene(const std::string& name, VulkanEngine* engine);  // @NOTE: when an entity is created, it is automatically connected to the engine
    bool saveScene(const std::string& name, const std::vector<Entity*>& entities, VulkanEngine* engine);
}
