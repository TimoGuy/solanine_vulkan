#include "HarvestableItem.h"

#include <iostream>
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "AudioEngine.h"
#include "DataSerialization.h"
#include "GlobalState.h"
#include "Textbox.h"
#include "Debug.h"
#include "imgui/imgui.h"


struct HarvestableItem_XData
{
    RenderObjectManager* rom;
    RenderObject* renderObj;
    vec3 position = GLM_VEC3_ZERO_INIT;
    size_t harvestableItemId = 0;
#ifdef _DEVELOP
    bool requestChangeItemModel = false;
#endif

    float_t interactionRadius = 3.0f;
    bool prevIsInteractible = false;  // Whether the player position is within the interaction field.
};


HarvestableItem::HarvestableItem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new HarvestableItem_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;

    if (ds)
        load(*ds);

    _data->renderObj =
        _data->rom->registerRenderObject({
            .model = _data->rom->getModel(globalState::getHarvestableItemByIndex(_data->harvestableItemId)->modelName, this, []() {}),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });
    glm_translate(_data->renderObj->transformMatrix, _data->position);
}

HarvestableItem::~HarvestableItem()
{
    // @COPYPASTA
    DataSerializer msg;
    msg.dumpString("msg_remove_interaction_request");
    msg.dumpString(getGUID());
    DataSerialized ds = msg.getSerializedData();
    _em->sendMessage(globalState::playerGUID, ds);
    /////////////

    _data->rom->unregisterRenderObject(_data->renderObj);
    _data->rom->removeModelCallbacks(this);

    delete _data;
}

void HarvestableItem::physicsUpdate(const float_t& physicsDeltaTime)
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
            msg.dumpString("harvest " + globalState::getHarvestableItemByIndex(_data->harvestableItemId)->name);
            DataSerialized ds = msg.getSerializedData();
            _em->sendMessage(globalState::playerGUID, ds);
        }
        else if (_data->prevIsInteractible && !isInteractible)
        {
            // @COPYPASTA
            DataSerializer msg;
            msg.dumpString("msg_remove_interaction_request");
            msg.dumpString(getGUID());
            DataSerialized ds = msg.getSerializedData();
            _em->sendMessage(globalState::playerGUID, ds);
        }
        _data->prevIsInteractible = isInteractible;
    }
}

void HarvestableItem::update(const float_t& deltaTime)
{
    if (_data->requestChangeItemModel)
    {
        // @BUG: validation error occurs right here... though, it'd just for development so not too much of a concern.
        // @COPYPASTA
        _data->rom->unregisterRenderObject(_data->renderObj);
        _data->rom->removeModelCallbacks(this);

        _data->renderObj =
            _data->rom->registerRenderObject({
                .model = _data->rom->getModel(globalState::getHarvestableItemByIndex(_data->harvestableItemId)->modelName, this, []() {}),
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = getGUID(),
                });

        _data->requestChangeItemModel = false;
    }
}

void HarvestableItem::lateUpdate(const float_t& deltaTime)
{
    glm_mat4_identity(_data->renderObj->transformMatrix);
    glm_translate(_data->renderObj->transformMatrix, _data->position);
}

void HarvestableItem::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(_data->position);
    float_t awii = (float_t)_data->harvestableItemId;
    ds.dumpFloat(awii);
}

void HarvestableItem::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadVec3(_data->position);
    float_t awii;
    ds.loadFloat(awii);
    _data->harvestableItemId = (size_t)awii;
}

bool HarvestableItem::processMessage(DataSerialized& message)
{
    std::string messageType;
    message.loadString(messageType);

    if (messageType == "msg_commit_interaction")
    {
        auto hitem = globalState::getHarvestableItemByIndex(_data->harvestableItemId);

        debug::pushDebugMessage({
            .message = "Harvested item " + hitem->name + ".",  // @TODO: have an in-game harvesting notification system. (Sim. to botw)
            });

        AudioEngine::getInstance().playSound("res/sfx/wip_item_get.wav");

        // Add item in inventory and destroy myself.
        globalState::changeInventoryItemQtyByIndex(_data->harvestableItemId, 1);
        _em->destroyEntity(this);

        DataSerializer msg;
        msg.dumpString("msg_notify_harvestable_item_harvested");
        DataSerialized ds = msg.getSerializedData();
        _em->sendMessage(globalState::playerGUID, ds);

        return true;
    }

    return false;
}

void HarvestableItem::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);
}

void HarvestableItem::renderImGui()
{
    int32_t hii = _data->harvestableItemId;
    if (ImGui::InputInt("harvestableItemId", &hii))
    {
        _data->harvestableItemId = (size_t)glm_clamp(hii, 0, globalState::getNumHarvestableItemIds() - 1);
        _data->requestChangeItemModel = true;
    }
}
