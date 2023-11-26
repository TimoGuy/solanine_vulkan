#pragma once

class VulkanEngine;
class Entity;
class DataSerialized;


namespace scene
{
    void init(VulkanEngine* engine);
    void tick();

    std::vector<std::string> getListOfEntityTypes();
    Entity* spinupNewObject(const std::string& objectName, DataSerialized* ds);

    const std::string SCENE_DIRECTORY_PATH = "res/scenes/";
    const std::string PREFAB_DIRECTORY_PATH = "res/prefabs/";
    std::vector<std::string> getListOfScenes();
    std::vector<std::string> getListOfPrefabs();
    bool loadPrefab(const std::string& name, std::vector<Entity*>& outEntityPtrs);
    bool loadPrefabNonOwned(const std::string& name);
    bool loadScene(const std::string& name, bool deleteExistingEntitiesFirst);  // @NOTE: when an entity is created, it is automatically connected to the engine
    bool saveScene(const std::string& name, const std::vector<Entity*>& entities);
}
