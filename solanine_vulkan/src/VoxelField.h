#pragma once

#include "Entity.h"
class VulkanEngine;
class EntityManager;
class RenderObjectManager;
struct VoxelField_XData;


class VoxelField : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    VoxelField(VulkanEngine* engine, EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~VoxelField();

    void simulationUpdate(float_t simDeltaTime) override;
    void update(float_t deltaTime);
    void lateUpdate(float_t deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);

    void reportMoved(mat4* matrixMoved);
    void renderImGui();

    void setBodyKinematic(bool isKinematic);
    void moveBody(vec3 newPosition, versor newRotation, bool immediate, float_t physicsDeltaTime);
    void getSize(vec3& outSize);
    void getTransform(mat4& outTransform);

private:
    VoxelField_XData* _data;
};
