#include <string>
#include <vector>
class VulkanEngine;
class Entity;


namespace scene
{
    bool loadScene(const std::string& fname, VulkanEngine* engine);  // @NOTE: when an entity is created, it is automatically connected to the engine
    bool saveScene(const std::string& fname, const std::vector<Entity*>& entities);
}
