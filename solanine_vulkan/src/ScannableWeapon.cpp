#include "ScannableWeapon.h"

#include <iostream>
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "DataSerialization.h"
#include "GlobalState.h"
#include "imgui/imgui.h"


struct ScannableWeapon_XData
{
    RenderObjectManager* rom;
    RenderObject* renderObj;
    vec3 position = GLM_VEC3_ZERO_INIT;
    float_t interactionRadius = 5.0f;
    bool prevIsInteractible = false;  // Whether the player position is within the interaction field.
};


ScannableWeapon::ScannableWeapon(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new ScannableWeapon_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;

    if (ds)
        load(*ds);

    vkglTF::Model* weaponModel = _data->rom->getModel("WingWeapon", this, []() {});
    _data->renderObj =
        _data->rom->registerRenderObject({
            .model = weaponModel,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });
    glm_translate(_data->renderObj->transformMatrix, _data->position);
}

ScannableWeapon::~ScannableWeapon()
{
    _data->rom->unregisterRenderObject(_data->renderObj);
    _data->rom->removeModelCallbacks(this);

    delete _data;
}

void ScannableWeapon::physicsUpdate(const float_t& physicsDeltaTime)
{
    // Check whether this is at an interactible distance away.
    if (!globalState::playerGUID.empty() &&
        globalState::playerPositionRef != nullptr)
    {
        bool isInteractible = (glm_vec3_distance2(*globalState::playerPositionRef, _data->position) < std::pow(_data->interactionRadius, 2.0f));
        if (isInteractible)
        {
            DataSerializer msg;
            msg.dumpString("msg_request_interaction");
            msg.dumpString(getGUID());
            msg.dumpString("scan weapon");
            DataSerialized ds = msg.getSerializedData();
            _em->sendMessage(globalState::playerGUID, ds);
        }
        else if (_data->prevIsInteractible && !isInteractible)
        {
            DataSerializer msg;
            msg.dumpString("msg_remove_interaction_request");
            msg.dumpString(getGUID());
            DataSerialized ds = msg.getSerializedData();
            _em->sendMessage(globalState::playerGUID, ds);
        }
        _data->prevIsInteractible = isInteractible;
    }
}

void ScannableWeapon::update(const float_t& deltaTime)
{
    
}

void ScannableWeapon::lateUpdate(const float_t& deltaTime)
{
    glm_mat4_identity(_data->renderObj->transformMatrix);
    glm_translate(_data->renderObj->transformMatrix, _data->position);
}

void ScannableWeapon::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(_data->position);
}

void ScannableWeapon::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadVec3(_data->position);
}

bool ScannableWeapon::processMessage(DataSerialized& message)
{
    return false;
}

void ScannableWeapon::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);
}

void ScannableWeapon::renderImGui()
{
    ImGui::Text("STBU");
}
