#pragma once

#include "Entity.h"
class EntityManager;
class RenderObjectManager;
struct Camera;
struct ScannableItem_XData;


class ScannableItem : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    ScannableItem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~ScannableItem();

    void simulationUpdate(float_t simDeltaTime) override;
    void update(float_t deltaTime);
    void lateUpdate(float_t deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void reportMoved(mat4* matrixMoved);
    void renderImGui();

private:
    ScannableItem_XData* _data;
};
