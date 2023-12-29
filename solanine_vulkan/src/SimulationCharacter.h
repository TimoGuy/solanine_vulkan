#pragma once

#include "Entity.h"
class EntityManager;
class RenderObject;
class RenderObjectManager;
struct Camera;
struct SimulationCharacter_XData;
namespace JPH
{
    class Body;
    class ContactManifold;
    class ContactSettings;
}


class SimulationCharacter : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    SimulationCharacter(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds);
    ~SimulationCharacter();

    void simulationUpdate(float_t simDeltaTime) override;
    void update(float_t deltaTime);
    void lateUpdate(float_t deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void reportMoved(mat4* matrixMoved);
    void renderImGui();

    void reportPhysicsContact(const JPH::Body& otherBody, const JPH::ContactManifold& manifold, JPH::ContactSettings* ioSettings);
    RenderObject* getMainRenderObject();

private:
    SimulationCharacter_XData* _data;
};
