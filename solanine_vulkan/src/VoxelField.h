#pragma once

#include "Entity.h"
class EntityManager;
class RenderObjectManager;
struct VoxelField_XData;


class VoxelField : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    VoxelField(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~VoxelField();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void update(const float_t& deltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);

    void reportMoved(void* matrixMoved);
    void renderImGui();

private:
    VoxelField_XData* _data;
};
