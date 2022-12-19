#pragma once

#include "Entity.h"
#include "Imports.h"
namespace vkglTF { struct Model; }
class     EntityManager;
struct    RenderObject;
class     RenderObjectManager;
struct    RegisteredPhysicsObject;


class Minecart : public Entity
{
public:
    static const std::string TYPE_NAME;

    Minecart(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~Minecart();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; }

    void loadModelWithName(const std::string& modelName);
    void createCollisionMeshFromModel();

    void reportMoved(void* matrixMoved);
    void renderImGui();

private:
    vkglTF::Model* _model;
    RenderObject* _renderObj = nullptr;
    RenderObjectManager* _rom = nullptr;
    RegisteredPhysicsObject* _physicsObj = nullptr;

    glm::mat4 _load_transform = glm::mat4(1.0f);

    // Tweak Props
    std::vector<glm::vec3> _bezierCurvePoints;
};
