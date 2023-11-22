#pragma once

#include "Entity.h"
class EntityManager;
class RenderObjectManager;
class VulkanEngine;
struct GondolaSystem_XData;


class GondolaSystem : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    static VulkanEngine* _engine;

    GondolaSystem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~GondolaSystem();

    void simulationUpdate(float_t simDeltaTime) override;
    void update(float_t deltaTime);
    void lateUpdate(float_t deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void reportMoved(mat4* matrixMoved);
    void renderImGui();


private:
    GondolaSystem_XData* _data;
};
