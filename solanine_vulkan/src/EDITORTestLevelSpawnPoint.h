#pragma once
#ifdef _DEVELOP

#include "Entity.h"
struct RenderObject;
class RenderObjectManager;
struct EDITORTestLevelSpawnPoint_XData;


class EDITORTestLevelSpawnPoint : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    EDITORTestLevelSpawnPoint(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~EDITORTestLevelSpawnPoint();

    void simulationUpdate(float_t simDeltaTime) override;

    void dump(DataSerializer& ds) override;
    void load(DataSerialized& ds) override;

    void reportMoved(mat4* matrixMoved) override;
    void renderImGui() override;

private:
    EDITORTestLevelSpawnPoint_XData* d;
};

#endif
