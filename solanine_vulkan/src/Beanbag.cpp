#include "Beanbag.h"

#include <iostream>
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "AudioEngine.h"
#include "PhysicsEngine.h"
#include "DataSerialization.h"
#include "GlobalState.h"
#include "Textbox.h"
#include "Debug.h"
#include "imgui/imgui.h"


struct Beanbag_XData
{
    RenderObjectManager* rom;
    RenderObject* renderObj;
    physengine::CapsulePhysicsData* cpd;
    vec3 position = GLM_VEC3_ZERO_INIT;
    mat4 rotation = GLM_MAT4_IDENTITY_INIT;
    float_t modelSize = 0.3f;
#ifdef _DEVELOP
    bool requestChangeItemModel = false;
#endif
};


Beanbag::Beanbag(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new Beanbag_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;

    if (ds)
        load(*ds);

    _data->renderObj =
        _data->rom->registerRenderObject({
            .model = _data->rom->getModel("Dummy", this, []() {}),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
        });
    glm_translate(_data->renderObj->transformMatrix, _data->position);

    _data->cpd = physengine::createCapsule(getGUID(), 1.0f, 1.0f);
    glm_vec3_copy(_data->position, _data->cpd->basePosition);
}

Beanbag::~Beanbag()
{
    physengine::destroyCapsule(_data->cpd);

    _data->rom->unregisterRenderObject(_data->renderObj);
    _data->rom->removeModelCallbacks(this);

    delete _data;
}

void Beanbag::physicsUpdate(const float_t& physicsDeltaTime)
{
    
}

void Beanbag::update(const float_t& deltaTime)
{
    if (_data->requestChangeItemModel)
    {
        // @BUG: validation error occurs right here... though, it'd just for development so not too much of a concern.
        // @COPYPASTA
        _data->rom->unregisterRenderObject(_data->renderObj);
        _data->rom->removeModelCallbacks(this);

        _data->renderObj =
            _data->rom->registerRenderObject({
                .model = _data->rom->getModel("Dummy", this, []() {}),
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = getGUID(),
            });

        _data->requestChangeItemModel = false;
    }
}

void Beanbag::lateUpdate(const float_t& deltaTime)
{
    glm_mat4_identity(_data->renderObj->transformMatrix);
    glm_translate(_data->renderObj->transformMatrix, _data->position);
    glm_mat4_mul(_data->renderObj->transformMatrix, _data->rotation, _data->renderObj->transformMatrix);
    glm_scale(_data->renderObj->transformMatrix, vec3{ _data->modelSize, _data->modelSize, _data->modelSize });
}

void Beanbag::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(_data->position);
    ds.dumpMat4(_data->rotation);
}

void Beanbag::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadVec3(_data->position);
    ds.loadMat4(_data->rotation);
}

bool Beanbag::processMessage(DataSerialized& message)
{
    std::string messageType;
    message.loadString(messageType);

    if (messageType == "msg_hitscan_hit")
    {
        // @TODO: get hit code right here!

        return true;
    }

    return false;
}

void Beanbag::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);
    glm_vec3_copy(_data->position, _data->cpd->basePosition);
    glm_mat4_copy(rot, _data->rotation);
}

void Beanbag::renderImGui()
{
    ImGui::DragFloat("cpd->radius", &_data->cpd->radius);
    ImGui::DragFloat("cpd->height", &_data->cpd->height);
    ImGui::DragFloat("modelSize", &_data->modelSize);
}
