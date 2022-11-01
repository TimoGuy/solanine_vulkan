#pragma once

#include "Entity.h"
#include "Imports.h"
namespace vkglTF { class Model; }
struct RenderObject;
struct RegisteredPhysicsObject;
class btPersistentManifold;
class btBoxShape;


class Yosemite : public Entity
{
public:
    static const std::string TYPE_NAME;

    Yosemite(VulkanEngine* engine, DataSerialized* ds);
    ~Yosemite();

    void physicsUpdate(const float_t& physicsDeltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; };

    void renderImGui();

private:
    vkglTF::Model* _cubeModel;
    RenderObject* _renderObj;
    RegisteredPhysicsObject* _physicsObj;

#ifdef _DEVELOP
    void updatePhysicsObjFromRenderTransform();
#endif

    glm::mat4 _load_renderTransform = glm::mat4(1.0f);
};
