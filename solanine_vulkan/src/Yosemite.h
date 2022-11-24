#pragma once

#include "Entity.h"
#include "Imports.h"
namespace vkglTF { struct Model; }
class EntityManager;
struct RenderObject;
class RenderObjectManager;
struct RegisteredPhysicsObject;
class btPersistentManifold;
class btBoxShape;


class Yosemite : public Entity
{
public:
    static const std::string TYPE_NAME;

    Yosemite(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~Yosemite();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; };

    void reportMoved(void* matrixMoved);
    void renderImGui();

private:
    vkglTF::Model* _cubeModel;
    RenderObject* _renderObj;
    RenderObjectManager* _rom;
    RegisteredPhysicsObject* _physicsObj;

    glm::mat4 _load_renderTransform = glm::mat4(1.0f);
    
    // Tweak Props
    bool      _isShallowPlanet       = false;
    float_t   _shallowPlanetMass     = 10.0f;
    float_t   _shallowPlanetLinDamp  = 0.0f;
    float_t   _shallowPlanetAngDamp  = 0.5f;
    float_t   _shallowPlanetAccel    = 0.5f;
    float_t   _shallowPlanetTorque   = 250.0f;
    glm::vec3 _shallowPlanetTargetPosition;
};
