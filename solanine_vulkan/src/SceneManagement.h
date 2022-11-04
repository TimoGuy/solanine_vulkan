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
    extern std::string currentLoadedScene;
    std::vector<std::string> getListOfScenes();
    bool loadScene(const std::string& name, VulkanEngine* engine);  // @NOTE: when an entity is created, it is automatically connected to the engine
    bool saveScene(const std::string& name, const std::vector<Entity*>& entities, VulkanEngine* engine);
}
