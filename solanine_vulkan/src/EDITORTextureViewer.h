#pragma once
#ifdef _DEVELOP

#include "Entity.h"
struct RenderObject;
class RenderObjectManager;
struct EDITORTextureViewer_XData;


class EDITORTextureViewer : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    EDITORTextureViewer(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~EDITORTextureViewer();

    void simulationUpdate(float_t simDeltaTime) override;

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);

    void teleportToPosition(vec3 position) { }
    void renderImGui() { }

    static void setAssignedMaterial(size_t uniqueMatBaseId, size_t derivedMatId);

private:
    EDITORTextureViewer_XData* d;
};

#endif
