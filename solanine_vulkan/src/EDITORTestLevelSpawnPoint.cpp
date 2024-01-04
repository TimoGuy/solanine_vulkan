#include "pch.h"

#include "EDITORTestLevelSpawnPoint.h"

#include "RenderObject.h"
#include "Camera.h"
#include "DataSerialization.h"
#include "GlobalState.h"


struct EDITORTestLevelSpawnPoint_XData
{
    RenderObjectManager* rom;
    RenderObject*        renderObj;

    int32_t spawnIdx        = 0;
    vec3    position        = GLM_VEC3_ZERO_INIT;
    float_t facingDirection = 0.0f;
};


void updateGlobalStateSpawns(EDITORTestLevelSpawnPoint_XData* d, EDITORTestLevelSpawnPoint* _this)
{
    for (auto& spd : globalState::listOfSpawnPoints)
        if (spd.referenceSpawnPointEntity == _this)
        {
            glm_vec3_copy(d->position, spd.position);
            spd.facingDirection = d->facingDirection;
            break;
        }
}


EDITORTestLevelSpawnPoint::EDITORTestLevelSpawnPoint(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), d(new EDITORTestLevelSpawnPoint_XData)
{
    Entity::_enableSimulationUpdate = true;

    d->rom = rom;

    if (ds)
        load(*ds);

    vkglTF::Model* model = d->rom->getModel("BuilderObj_SpawnPosition", this, [](){});
    d->rom->registerRenderObjects({
            {
                .model = model,
                .renderLayer = RenderLayer::BUILDER,
                .attachedEntityGuid = getGUID(),
            }
        },
        { &d->renderObj }
    );

    globalState::listOfSpawnPoints.push_back({
        .referenceSpawnPointEntity = this,
    });

    updateGlobalStateSpawns(d, this);
}

EDITORTestLevelSpawnPoint::~EDITORTestLevelSpawnPoint()
{
    d->rom->unregisterRenderObjects({ d->renderObj });
    d->rom->removeModelCallbacks(this);

    std::erase_if(
        globalState::listOfSpawnPoints,
        [&](globalState::SpawnPointData spd) {
            return spd.referenceSpawnPointEntity == this;
        }
    );
}

void EDITORTestLevelSpawnPoint::simulationUpdate(float_t simDeltaTime)
{
    // Reconstruct transform matrix.
    vec3 eulerAngles = { 0.0f, d->facingDirection, 0.0f };
    mat4 rotation;
    glm_euler_zyx(eulerAngles, rotation);

    auto& tm = d->renderObj->transformMatrix;
    glm_mat4_identity(tm);
    glm_translate(tm, d->position);
    glm_mul_rot(tm, rotation, tm);
}

void EDITORTestLevelSpawnPoint::dump(DataSerializer& ds)
{
    Entity::dump(ds);

    ds.dumpFloat((float_t)d->spawnIdx);
    ds.dumpVec3(d->position);
    ds.dumpFloat(d->facingDirection);
}

void EDITORTestLevelSpawnPoint::load(DataSerialized& ds)
{
    Entity::load(ds);

    float_t spawnIdxF;
    ds.loadFloat(spawnIdxF);
    d->spawnIdx = (int32_t)spawnIdxF;
    ds.loadVec3(d->position);
    ds.loadFloat(d->facingDirection);
}

void EDITORTestLevelSpawnPoint::teleportToPosition(vec3 position)
{
    glm_vec3_copy(position, d->position);
    updateGlobalStateSpawns(d, this);
}

void EDITORTestLevelSpawnPoint::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, d->position);
    vec3 forward;
    glm_mat4_mulv3(rot, vec3{ 0.0f, 0.0f, 1.0f }, 0.0f, forward);
    d->facingDirection = atan2f(forward[0], forward[2]);

    updateGlobalStateSpawns(d, this);
}

void EDITORTestLevelSpawnPoint::renderImGui()
{
    if (ImGui::InputInt("spawnIdx", &d->spawnIdx))
        d->spawnIdx = std::max(0, std::min(128, d->spawnIdx));
}
