#pragma once

#include "Entity.h"
class EntityManager;
class RenderObjectManager;
struct Camera;
struct HarvestableItem_XData;


class HarvestableItem : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    HarvestableItem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~HarvestableItem();

    void simulationUpdate(float_t simDeltaTime) override;
    void update(float_t deltaTime);
    void lateUpdate(float_t deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void teleportToPosition(vec3 position) override;
    void reportMoved(mat4* matrixMoved);
    void renderImGui();

private:
    HarvestableItem_XData* _data;
};
