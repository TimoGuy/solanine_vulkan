#include "ScannableItem.h"

#include <iostream>
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "DataSerialization.h"
#include "GlobalState.h"
#include "Textbox.h"
#include "imgui/imgui.h"


struct ScannableItem_XData
{
    RenderObjectManager* rom;
    RenderObject* renderObj;
    vec3 position = GLM_VEC3_ZERO_INIT;
    size_t scannableItemId = 0;
#ifdef _DEVELOP
    bool requestChangeItemModel = false;
#endif

    float_t interactionRadius = 5.0f;
    bool prevIsInteractible = false;  // Whether the player position is within the interaction field.
};


ScannableItem::ScannableItem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new ScannableItem_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;

    if (ds)
        load(*ds);

    _data->renderObj =
        _data->rom->registerRenderObject({
            .model = _data->rom->getModel(globalState::getAncientWeaponItemByIndex(_data->scannableItemId)->modelName, this, []() {}),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });
    glm_translate(_data->renderObj->transformMatrix, _data->position);
}

ScannableItem::~ScannableItem()
{
    _data->rom->unregisterRenderObject(_data->renderObj);
    _data->rom->removeModelCallbacks(this);

    delete _data;
}

void ScannableItem::physicsUpdate(const float_t& physicsDeltaTime)
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
            msg.dumpString("scan " + globalState::ancientWeaponItemTypeToString(globalState::getAncientWeaponItemByIndex(_data->scannableItemId)->type));
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

void ScannableItem::update(const float_t& deltaTime)
{
    if (_data->requestChangeItemModel)
    {
        // @BUG: validation error occurs right here... though, it'd just for development so not too much of a concern.
        _data->rom->unregisterRenderObject(_data->renderObj);
        _data->rom->removeModelCallbacks(this);

        _data->renderObj =
        _data->rom->registerRenderObject({
            .model = _data->rom->getModel(globalState::getAncientWeaponItemByIndex(_data->scannableItemId)->modelName, this, []() {}),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

        _data->requestChangeItemModel = false;
    }
}

void ScannableItem::lateUpdate(const float_t& deltaTime)
{
    glm_mat4_identity(_data->renderObj->transformMatrix);
    glm_translate(_data->renderObj->transformMatrix, _data->position);
}

void ScannableItem::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(_data->position);
    float_t awii = (float_t)_data->scannableItemId;
    ds.dumpFloat(awii);
}

void ScannableItem::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadVec3(_data->position);
    float_t awii;
    ds.loadFloat(awii);
    _data->scannableItemId = (size_t)awii;
}

bool ScannableItem::processMessage(DataSerialized& message)
{
    std::string messageType;
    message.loadString(messageType);

    if (messageType == "msg_commit_interaction")
    {
        auto awi = globalState::getAncientWeaponItemByIndex(_data->scannableItemId);

        std::string materializationReqLine = "To materialize:";
        for (auto req : awi->requiredMaterialsToMaterialize)
            materializationReqLine += "\n" + globalState::getHarvestableItemByIndex(req.harvestableItemId)->name + " (x" + std::to_string(req.quantity) + ")";

        textbox::sendTextboxMessage({
            .texts = {
                "Item scanned.",
                "This is a " + globalState::ancientWeaponItemTypeToString(awi->type) + ":\n\"" + awi->name + "\".",
                materializationReqLine,
                "Press 'LMB'\nto materialize and use.",
            },
            .useEndingQuery = false,
        });

        // Flag this item as materializable in ancient weapon.  @FUTURE: have a "limited memory" gameplay system, where you have to organize the memory that the new item takes up.
        globalState::flagScannableItemAsCanMaterializeByIndex(_data->scannableItemId, true);
        globalState::setSelectedScannableItemId(_data->scannableItemId);

        DataSerializer msg;
        msg.dumpString("msg_notify_scannable_item_added");
        DataSerialized ds = msg.getSerializedData();
        _em->sendMessage(globalState::playerGUID, ds);

        return true;
    }

    return false;
}

void ScannableItem::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);
}

void ScannableItem::renderImGui()
{
    int32_t awii = _data->scannableItemId;
    if (ImGui::InputInt("scannableItemId", &awii))
    {
        _data->scannableItemId = (size_t)glm_clamp(awii, 0, globalState::getNumScannableItemIds() - 1);
        _data->requestChangeItemModel = true;
    }
}
