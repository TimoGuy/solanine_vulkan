#pragma once

#include "Entity.h"
class EntityManager;
class RenderObjectManager;
struct Camera;
struct Beanbag_XData;


class Beanbag : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    Beanbag(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~Beanbag();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void update(const float_t& deltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void reportMoved(mat4* matrixMoved);
    void renderImGui();

private:
    Beanbag_XData* _data;
};
