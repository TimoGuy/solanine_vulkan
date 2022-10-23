#include "ModelCache.h"

#include "VulkanEngine.h"


vkglTF::Model* ModelCache::getModel(VulkanEngine* engine, const std::string& filename, float scale)
{
    // Give already loaded model
    auto it = _modelCache.find(filename);
    if (it != _modelCache.end())
        return &_modelCache[filename];

    // Load new model
    vkglTF::Model newModel;
    newModel.loadFromFile(engine, filename, scale);
    _modelCache[filename] = newModel;
    return &_modelCache[filename];
}
