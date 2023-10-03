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

    GondolaSystem(EntityManager* em, RenderObjectManager* rom, VulkanEngine* engineRef, DataSerialized* ds);
    ~GondolaSystem();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void update(const float_t& deltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void reportMoved(mat4* matrixMoved);
    void renderImGui();

private:
    GondolaSystem_XData* _data;
};
