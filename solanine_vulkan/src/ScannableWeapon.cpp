#include "ScannableWeapon.h"

#include <iostream>
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "DataSerialization.h"
#include "GlobalState.h"
#include "Textbox.h"
#include "imgui/imgui.h"


struct ScannableWeapon_XData
{
    RenderObjectManager* rom;
    RenderObject* renderObj;
    vec3 position = GLM_VEC3_ZERO_INIT;
    std::string itemModel = "WingWeapon";
    std::string itemName = "Wing Blade";
    std::string itemType = "weapon";

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

    vkglTF::Model* weaponModel = _data->rom->getModel(_data->itemModel, this, []() {});
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
    ds.dumpString(_data->itemModel);
    ds.dumpString(_data->itemName);
    ds.dumpString(_data->itemType);
}

void ScannableWeapon::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadVec3(_data->position);
    ds.loadString(_data->itemModel);
    ds.loadString(_data->itemName);
    ds.loadString(_data->itemType);
}

bool ScannableWeapon::processMessage(DataSerialized& message)
{
    std::string messageType;
    message.loadString(messageType);

    if (messageType == "msg_commit_interaction")
    {
        textbox::sendTextboxMessage({
            .texts = {
                "Item scanned.",
                "You now have the " + _data->itemType + ":\n\"" + _data->itemName + "\".",
                "Press 'LMB'\nto materialize and use.",
            },
            .useEndingQuery = false,
        });

        DataSerializer msg;
        msg.dumpString("msg_add_item_to_ancient_weapon");
        msg.dumpString(_data->itemName);
        msg.dumpString(_data->itemType);
        msg.dumpFloat(0);   // @TODO: this is supposed to be the position in "memory" of the ancient weapon that the item starts at.
        msg.dumpFloat(10);  //        This... is the size of "memory" this item takes up.
        DataSerialized ds = msg.getSerializedData();
        _em->sendMessage(globalState::playerGUID, ds);

        return true;
    }

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
