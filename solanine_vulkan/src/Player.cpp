#include "Player.h"

#include <cstdlib>
#include "Imports.h"
#include "PhysUtil.h"
#include "PhysicsEngine.h"
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "TextMesh.h"
#include "Textbox.h"
#include "Camera.h"
#include "InputManager.h"
#include "AudioEngine.h"
#include "DataSerialization.h"
#include "GlobalState.h"
#include "Debug.h"
#include "imgui/imgui.h"


struct Player_XData
{
    RenderObjectManager*     rom;
    Camera*                  camera;
    RenderObject*            characterRenderObj;
    RenderObject*            handleRenderObj;
    RenderObject*            weaponRenderObj;
    std::string              weaponAttachmentJointName;

    physengine::CapsulePhysicsData* cpd;

    textmesh::TextMesh* uiMaterializeItem;  // @NOTE: This is a debug ui thing. In the real thing, I'd really want there to be in the bottom right the ancient weapon handle pointing vertically with the materializing item in a wireframe with a mysterious blue hue at the end of the handle, and when the item does get materialized, it becomes a rendered version.
    std::string currentUIMaterializeItemText = "";
    globalState::ScannableItemOption* materializedItem = nullptr;

    vec3 worldSpaceInput = GLM_VEC3_ZERO_INIT;
    float_t gravityForce = 0.0f;
    bool    inputFlagJump = false;
    bool    inputFlagAttack = false;
    bool    inputFlagRelease = false;
    float_t attackTwitchAngle = 0.0f;
    float_t attackTwitchAngleReturnSpeed = 3.0f;
    bool    prevIsGrounded = false;
    vec3    prevGroundNormal = GLM_VEC3_ZERO_INIT;

    // Tweak Props
    vec3 position;
    float_t facingDirection = 0.0f;
    float_t modelSize = 0.3f;
};

std::string getUIMaterializeItemText(Player_XData* d)
{
    if (d->materializedItem == nullptr)
    {
        std::string text = "No item to materialize";
        size_t sii = globalState::getSelectedScannableItemId();
        if (globalState::getCanMaterializeScannableItemByIndex(sii))
        {
            text = "";
            globalState::ScannableItemOption* sio = globalState::getAncientWeaponItemByIndex(sii);
            for (globalState::HarvestableItemWithQuantity& hiwq : sio->requiredMaterialsToMaterialize)
                text +=
                    "(" + std::to_string(globalState::getInventoryQtyOfHarvestableItemByIndex(hiwq.harvestableItemId)) + "/" + std::to_string(hiwq.quantity) + ") " + globalState::getHarvestableItemByIndex(hiwq.harvestableItemId)->name + "\n";
            text += "Press LMB to materialize " + sio->name;
        }
        return text;
    }
    else
    {
        std::string text = "Press LMB to use " + d->materializedItem->name;
        return text;
    }
}

void processAttack(Player_XData* d)
{
    if (d->materializedItem == nullptr)
    {
        // Attempt to materialize item.
        size_t sii = globalState::getSelectedScannableItemId();
        if (globalState::getCanMaterializeScannableItemByIndex(sii))
        {
            // Check if have enough materials
            globalState::ScannableItemOption* sio = globalState::getAncientWeaponItemByIndex(sii);
            bool canMaterialize = true;
            for (globalState::HarvestableItemWithQuantity& hiwq : sio->requiredMaterialsToMaterialize)
                if (globalState::getInventoryQtyOfHarvestableItemByIndex(hiwq.harvestableItemId) < hiwq.quantity)
                {
                    canMaterialize = false;
                    break;
                }
            
            // Materialize item!
            if (canMaterialize)
            {
                for (globalState::HarvestableItemWithQuantity& hiwq : sio->requiredMaterialsToMaterialize)
                    globalState::changeInventoryItemQtyByIndex(hiwq.harvestableItemId, -(int32_t)hiwq.quantity);  // Remove from inventory the materials needed.
                d->materializedItem = sio;
                AudioEngine::getInstance().playSound("res/sfx/wip_Pl_Kago_Ready.wav");
            }
            else
            {
                debug::pushDebugMessage({
                    .message = "Not enough materials for materialization.",
                });
            }
        }
        else
        {
            debug::pushDebugMessage({
                .message = "No item is selected to materialize.",
            });
        }
    }
    else
    {
        // Attempt to use item.
        switch (d->materializedItem->type)
        {
            case globalState::WEAPON:
            {

                // Attack rhythm failed. Twitch and take away stamina.
                d->attackTwitchAngle = (float_t)std::rand() / (RAND_MAX / 2.0f) > 0.5f ? glm_rad(2.0f) : glm_rad(-2.0f);
            } break;

            case globalState::FOOD:
            {
                // Attempt to eat.
                globalState::savedPlayerHealth += 5;
                d->materializedItem = nullptr;  // Ate the item off the handle.
                AudioEngine::getInstance().playSound("res/sfx/wip_Pl_Eating_S00.wav");
                AudioEngine::getInstance().playSound("res/sfx/wip_Sys_ExtraHeartUp_01.wav");
            } break;

            case globalState::TOOL:
            {

                // Attempt to use tool.
                // @NOTE: in the future may combine weapon and tool classifications as far as this branching goes.
            } break;
        }
    }

    // Update ui text
    textmesh::regenerateTextMeshMesh(d->uiMaterializeItem, getUIMaterializeItemText(d));
}

void processRelease(Player_XData* d)
{
    d->materializedItem = nullptr;  // Release the item off the handle.
    // @TODO: leave the item on the ground if you wanna reattach or use or litter.
    AudioEngine::getInstance().playSoundFromList({
        "res/sfx/wip_Pl_IceBreaking00.wav",
        "res/sfx/wip_Pl_IceBreaking01.wav",
        "res/sfx/wip_Pl_IceBreaking02.wav",
    });
    textmesh::regenerateTextMeshMesh(d->uiMaterializeItem, getUIMaterializeItemText(d));
}

Player::Player(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds) : Entity(em, ds), _data(new Player_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;
    _data->camera = camera;

    if (ds)
        load(*ds);

    _data->weaponAttachmentJointName = "Back Attachment";
    std::vector<vkglTF::Animator::AnimatorCallback> animatorCallbacks = {
        {
            "EventPlaySFXAttack", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_MM_Link_Attack1.wav",
                    "res/sfx/wip_MM_Link_Attack2.wav",
                    "res/sfx/wip_MM_Link_Attack3.wav",
                    "res/sfx/wip_MM_Link_Attack4.wav",
                    //"res/sfx/wip_hollow_knight_sfx/hero_nail_art_great_slash.wav",
                });
            }
        },
        {
            "EventPlaySFXLandHard", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_OOT_Link_FallDown_Wood.wav",
                });
            }
        },
        {
            "EventPlaySFXGrabbed", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_OOT_Link_Freeze.wav",
                });
            }
        },
        {
            "EventAllowComboInput", [&]() {
                // _allowComboInput = true;
            }
        },
        {
            "EventAllowComboTransition", [&]() {
                // _allowComboTransition = true;
            }
        },
        {
            "EventGotoEndAttackStage", [&]() {
                // _attackStage = AttackStage::END;
                // _flagAttack = false;  // To prevent unusual behavior (i.e. had a random attack just start from the beginning despite no inputs. So this is just to make sure)
            }
        },
        {
            "EventGotoNoneAttackStage", [&]() {
                // _attackStage = AttackStage::NONE;
                // _flagAttack = false;  // To prevent unusual behavior (i.e. had a random attack just start from the beginning despite no inputs. So this is just to make sure)
            }
        },
        /*{
            "EventEnableBroadSensingAttack1", [&]() {
                _enableBroadSensingAttack1Timer = 0.1f;  // How long to have the broad sensing attack on (this prevents grabs and attacks from enemies until it's off)
            }
        },
        {
            "EventEnableNarrowSensingAttack1", [&]() {
                _enableNarrowSensingAttack1 = true;  // @NOTE: this is an "animation" that's run in the physicsupdate() routine. The `true` is essentially just a flag.
            }
        },*/
    };

    vkglTF::Model* characterModel = _data->rom->getModel("SlimeGirl", this, [](){});
    _data->characterRenderObj =
        _data->rom->registerRenderObject({
            .model = characterModel,
            .animator = new vkglTF::Animator(characterModel, animatorCallbacks),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });
    // transformMatrix = glm::translate(GLM_MAT4_IDENTITY_INIT, _data->position) * glm::toMat4(glm::quat(vec3(0, _data->facingDirection, 0))) * glm::scale(GLM_MAT4_IDENTITY_INIT, vec3(_data->modelSize)),

    vkglTF::Model* handleModel = _data->rom->getModel("Handle", this, [](){});
    _data->handleRenderObj =
        _data->rom->registerRenderObject({
            .model = handleModel,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    vkglTF::Model* weaponModel = _data->rom->getModel("WingWeapon", this, [](){});
    _data->weaponRenderObj =
        _data->rom->registerRenderObject({
            .model = weaponModel,
            .renderLayer = RenderLayer::INVISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    _data->camera->mainCamMode.setMainCamTargetObject(_data->characterRenderObj);  // @NOTE: I believe that there should be some kind of main camera system that targets the player by default but when entering different volumes etc. the target changes depending.... essentially the system needs to be more built out imo

    _data->cpd = physengine::createCapsule(0.5f, 1.0f);  // Total height is 2, but r*2 is subtracted to get the capsule height (i.e. the line segment length that the capsule rides along)
    glm_vec3_copy(_data->position, _data->cpd->basePosition);

    globalState::playerGUID = getGUID();
    globalState::playerPositionRef = &_data->cpd->basePosition;

    _data->uiMaterializeItem = textmesh::createAndRegisterTextMesh("defaultFont", textmesh::RIGHT, textmesh::BOTTOM, getUIMaterializeItemText(_data));
    _data->uiMaterializeItem->isPositionScreenspace = true;
    glm_vec3_copy(vec3{ 925.0f, -510.0f, 0.0f }, _data->uiMaterializeItem->renderPosition);
    _data->uiMaterializeItem->scale = 25.0f;
}

Player::~Player()
{
    textmesh::destroyAndUnregisterTextMesh(_data->uiMaterializeItem);

    if (globalState::playerGUID == getGUID() ||
        globalState::playerPositionRef == &_data->cpd->basePosition)
    {
        globalState::playerGUID = "";
        globalState::playerPositionRef = nullptr;
    }

    delete _data->characterRenderObj->animator;
    _data->rom->unregisterRenderObject(_data->characterRenderObj);
    _data->rom->unregisterRenderObject(_data->handleRenderObj);
    _data->rom->unregisterRenderObject(_data->weaponRenderObj);
    _data->rom->removeModelCallbacks(this);

    physengine::destroyCapsule(_data->cpd);

    delete _data;
}

void Player::physicsUpdate(const float_t& physicsDeltaTime)
{
    if (textbox::isProcessingMessage())
    {
        _data->uiMaterializeItem->excludeFromBulkRender = true;
        return;
    }
    else
        _data->uiMaterializeItem->excludeFromBulkRender = false;

    //
    // Calculate input
    //
    vec2 input = GLM_VEC2_ZERO_INIT;
    input[0] += input::keyLeftPressed  ? -1.0f : 0.0f;
    input[0] += input::keyRightPressed ?  1.0f : 0.0f;
    input[1] += input::keyUpPressed    ?  1.0f : 0.0f;
    input[1] += input::keyDownPressed  ? -1.0f : 0.0f;

    if (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
    {
        input[0] = input[1] = 0.0f;
        _data->inputFlagJump = false;
    }

    vec3 flatCameraFacingDirection = {
        _data->camera->sceneCamera.facingDirection[0],
        0.0f,
        _data->camera->sceneCamera.facingDirection[2]
    };
    glm_normalize(flatCameraFacingDirection);

    glm_vec3_scale(flatCameraFacingDirection, input[1], _data->worldSpaceInput);
    vec3 up = { 0.0f, 1.0f, 0.0f };
    vec3 flatCamRight;
    glm_vec3_cross(flatCameraFacingDirection, up, flatCamRight);
    glm_normalize(flatCamRight);
    glm_vec3_muladds(flatCamRight, input[0], _data->worldSpaceInput);

    if (glm_vec3_norm2(_data->worldSpaceInput) < 0.01f)
        glm_vec3_zero(_data->worldSpaceInput);
    else
    {
        float_t magnitude = glm_clamp_zo(glm_vec3_norm(_data->worldSpaceInput));
        glm_vec3_scale_as(_data->worldSpaceInput, magnitude, _data->worldSpaceInput);
        _data->facingDirection = atan2f(_data->worldSpaceInput[0], _data->worldSpaceInput[2]);
    }


    //
    // Update movement and collision
    //
    constexpr float_t gravity = -0.98f / 0.025f;  // @TODO: put physicsengine constexpr of `physicsDeltaTime` into the header file and rename it to `constantPhysicsDeltaTime` and replace the 0.025f with it.
    constexpr float_t jumpHeight = 2.0f;
    _data->gravityForce += gravity * physicsDeltaTime;
    if (_data->prevIsGrounded && _data->inputFlagJump)
    {
        _data->gravityForce = std::sqrtf(jumpHeight * 2.0f * std::abs(gravity));
        _data->prevIsGrounded = false;
        _data->inputFlagJump = false;
    }

    vec3 velocity;
    glm_vec3_scale(_data->worldSpaceInput, 10.0f * physicsDeltaTime, velocity);

    if (_data->prevIsGrounded && _data->prevGroundNormal[1] < 0.999f)
    {
        versor groundNormalRotation;
        glm_quat_from_vecs(vec3{ 0.0f, 1.0f, 0.0f }, _data->prevGroundNormal, groundNormalRotation);
        mat3 groundNormalRotationM3;
        glm_quat_mat3(groundNormalRotation, groundNormalRotationM3);
        glm_mat3_mulv(groundNormalRotationM3, velocity, velocity);
    }

    glm_vec3_add(velocity, vec3{ 0.0f, _data->gravityForce * physicsDeltaTime, 0.0f }, velocity);
    physengine::moveCapsuleAccountingForCollision(*_data->cpd, velocity, _data->prevIsGrounded, _data->prevGroundNormal);
    glm_vec3_copy(_data->cpd->basePosition, _data->position);

    _data->prevIsGrounded = (_data->prevGroundNormal[1] >= 0.707106781187);  // >=45 degrees
    if (_data->prevIsGrounded)
        _data->gravityForce = 0.0f;

    //
    // Update Attack
    //
    if (_data->inputFlagAttack)
    {
        processAttack(_data);
        _data->inputFlagAttack = false;
    }

    if (_data->inputFlagRelease)
    {
        processRelease(_data);
        _data->inputFlagRelease = false;
    }
}

// @TODO: @INCOMPLETE: will need to move the interaction logic into its own type of object, where you can update the interactor position and add/register interaction fields.
// @COMMENT: hey, i think this should be attached to the player, not some global logic. Using the messaging system to add interaction guids makes sense to me.
struct GUIDWithVerb
{
    std::string guid, actionVerb;
};
std::vector<GUIDWithVerb> interactionGUIDPriorityQueue;
textmesh::TextMesh* interactionUIText;
std::string currentText;

void Player::update(const float_t& deltaTime)
{
    //
    // Handle 'E' action
    //
    if (interactionUIText != nullptr &&
        !interactionGUIDPriorityQueue.empty())
        if (_data->prevIsGrounded && !textbox::isProcessingMessage())
        {
            interactionUIText->excludeFromBulkRender = false;
            if (input::onKeyInteractPress)
            {
                DataSerializer ds;
                ds.dumpString("msg_commit_interaction");
                DataSerialized dsd = ds.getSerializedData();
                _em->sendMessage(interactionGUIDPriorityQueue.front().guid, dsd);
            }
        }
        else
            interactionUIText->excludeFromBulkRender = true;

    if (textbox::isProcessingMessage())
        return;

    _data->inputFlagJump |= input::onKeyJumpPress;
    _data->inputFlagAttack |= input::onLMBPress;
    _data->inputFlagRelease |= input::onRMBPress;

    //
    // Update mask for animation
    // @TODO: there is popping for some reason. Could be how the transitions/triggers work in the animator controller or could be a different underlying issue. Figure it out pls!  -Timo
    //
    _data->characterRenderObj->animator->setMask(
        "MaskCombatMode",
        false
    );
    
    // Update twitch angle
    _data->characterRenderObj->animator->setTwitchAngle(_data->attackTwitchAngle);
    _data->attackTwitchAngle = glm_lerp(_data->attackTwitchAngle, 0.0f, std::abs(_data->attackTwitchAngle) * _data->attackTwitchAngleReturnSpeed * 60.0f * deltaTime);
}

void Player::lateUpdate(const float_t& deltaTime)
{
    //
    // Update position of character and weapon
    //
    vec3 eulerAngles = { 0.0f, _data->facingDirection, 0.0f };
    mat4 rotation;
    glm_euler_zyx(eulerAngles, rotation);
    vec3 scale = { _data->modelSize, _data->modelSize, _data->modelSize };

    mat4 transform = GLM_MAT4_IDENTITY_INIT;
    glm_translate(transform, _data->cpd->interpolBasePosition);
    glm_mat4_mul(transform, rotation, transform);
    glm_scale(transform, scale);
    glm_mat4_copy(transform, _data->characterRenderObj->transformMatrix);

    mat4 attachmentJointMat;
    _data->characterRenderObj->animator->getJointMatrix(_data->weaponAttachmentJointName, attachmentJointMat);
    glm_mat4_mul(_data->characterRenderObj->transformMatrix, attachmentJointMat, _data->weaponRenderObj->transformMatrix);
    glm_mat4_copy(_data->weaponRenderObj->transformMatrix, _data->handleRenderObj->transformMatrix);
}

void Player::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(_data->position);
    ds.dumpFloat(_data->facingDirection);
}

void Player::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadVec3(_data->position);
    ds.loadFloat(_data->facingDirection);
}

void updateInteractionUI()
{
    // Initial creation of the UI.
    if (interactionUIText == nullptr)
    {
        currentText = "";
        interactionUIText = textmesh::createAndRegisterTextMesh("defaultFont", textmesh::CENTER, textmesh::MID, currentText);
        interactionUIText->isPositionScreenspace = true;
        glm_vec3_copy(vec3{ 0.0f, -50.0f, 0.0f }, interactionUIText->renderPosition);
        interactionUIText->scale = 25.0f;
    }

    // Update UI text and visibility.
    std::string newText = interactionGUIDPriorityQueue.empty() ? "" : ("Press 'E' to " + interactionGUIDPriorityQueue.front().actionVerb);
    if (currentText != newText)
    {
        currentText = newText;
        textmesh::regenerateTextMeshMesh(interactionUIText, currentText);
    }

    interactionUIText->excludeFromBulkRender = currentText.empty();
}

bool Player::processMessage(DataSerialized& message)
{
    std::string messageType;
    message.loadString(messageType);

    if (messageType == "msg_request_interaction")
    {
        std::string guid, actionVerb;
        message.loadString(guid);
        message.loadString(actionVerb);

        // Add to queue if not already in. Front is the current interaction field.
        bool guidExists = false;
        for (auto& gwv : interactionGUIDPriorityQueue)
            if (gwv.guid == guid)
            {
                guidExists = true;
                break;
            }
        if (!guidExists)
        {
            interactionGUIDPriorityQueue.push_back({
                .guid = guid,
                .actionVerb = actionVerb,
            });
            updateInteractionUI();
        }

        return true;
    }
    else if (messageType == "msg_remove_interaction_request")
    {
        std::string guid;
        message.loadString(guid);

        // Remove the interaction request from the guid queue.
        std::erase_if(
            interactionGUIDPriorityQueue,
            [guid](GUIDWithVerb& gwv) {
                return gwv.guid == guid;
            }
        );
        updateInteractionUI();

        return true;
    }
    else if (messageType == "msg_notify_scannable_item_added" || messageType == "msg_notify_harvestable_item_harvested")
    {
        textmesh::regenerateTextMeshMesh(_data->uiMaterializeItem, getUIMaterializeItemText(_data));
        return true;
    }

    return false;
}

void Player::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);
    glm_vec3_copy(_data->position, _data->cpd->basePosition);
}

void Player::renderImGui()
{
    ImGui::DragFloat("modelSize", &_data->modelSize);
    ImGui::DragFloat("attackTwitchAngleReturnSpeed", &_data->attackTwitchAngleReturnSpeed);
    ImGui::DragFloat3("uiMaterializeItem->renderPosition", _data->uiMaterializeItem->renderPosition);
}
