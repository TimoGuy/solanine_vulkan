#pragma once

#include "Entity.h"
class EntityManager;
class RenderObjectManager;
struct Camera;
struct Character_XData;
namespace JPH
{
    class Body;
    class ContactManifold;
    class ContactSettings;
}


class Character : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    Character(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds);
    ~Character();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void update(const float_t& deltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void reportMoved(mat4* matrixMoved);
    void renderImGui();

    void reportPhysicsContact(const JPH::Body& otherBody, const JPH::ContactManifold& manifold, JPH::ContactSettings* ioSettings);

private:
    Character_XData* _data;
};
