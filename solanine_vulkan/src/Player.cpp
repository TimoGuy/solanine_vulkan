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
    globalState::ScannableItemOption* materializedItem = nullptr;

    textmesh::TextMesh* uiStamina;
    struct StaminaData
    {
        int16_t currentStamina;
        int16_t maxStamina = 100;
        float_t refillTime = 0.5f;  // Wait this time before starting to refill stamina.
        float_t refillTimer = 0.0f;
        float_t changedTime = 0.5f;  // Wait this time before disappearing after a stamina change occurred.
        float_t changedTimer = 0.0f;
        int16_t refillRate = 50;
    } staminaData;

    struct AttackWaza
    {
        std::string animationTrigger;
        int16_t staminaCost = 10;
        float_t duration = 0.0f;
        
        struct HitscanFlowNode
        {
            // These ends create a line where `numHitscanSamples` number of points traverse.
            // These points are connected to the previous node's ends' traversed lines to create
            // the hitscan query lines. Note also that these points are in object space,
            // where { 0, 0, 1 } represents the player's facing forward vector.
            vec3 nodeEnd1, nodeEnd2;
            float_t executeAtTime = 0.0f;
        };
        uint32_t numHitscanSamples = 5;
        std::vector<HitscanFlowNode> hitscanNodes;  // Each node uses the previous node's data to create the hitscans (the first node is ignored except for using it as prev node data).

        struct Chain
        {
            float_t inputTimeWindowStart = 0.0f;  // Press the attack button in this window to trigger the chain.
            float_t inputTimeWindowEnd = 0.0f;
            AttackWaza* nextAttack = nullptr;
        };
        std::vector<Chain> chains;  // Note that you can have different chains depending on your rhythm in the attack.
    };
    AttackWaza  rootWaza;

    AttackWaza* currentWaza = nullptr;
    float_t     wazaTimer = 0.0f;  // Used for timing chains and hitscans.
    size_t      wazaCurrentHitScanIdx = 1;  // 0th hitscan node is ignored.

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

void processWeaponAttackInput(Player_XData* d);

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

std::string getStaminaText(Player_XData* d)
{
    return "Stamina: " + std::to_string(d->staminaData.currentStamina) + "/" + std::to_string(d->staminaData.maxStamina);
}

void changeStamina(Player_XData* d, int16_t amount)
{
    d->staminaData.currentStamina += amount;
    d->staminaData.currentStamina = std::clamp(d->staminaData.currentStamina, (int16_t)0, d->staminaData.maxStamina);

    if (amount < 0)
        d->staminaData.refillTimer = d->staminaData.refillTime;
    d->staminaData.changedTimer = d->staminaData.changedTime;

    textmesh::regenerateTextMeshMesh(d->uiStamina, getStaminaText(d));
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
    else if (d->staminaData.currentStamina > 0)
    {
        // Attempt to use item.
        switch (d->materializedItem->type)
        {
            case globalState::WEAPON:
            {
                processWeaponAttackInput(d);
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
    if (d->materializedItem == nullptr)
        return;

    d->materializedItem = nullptr;  // Release the item off the handle.
    // @TODO: leave the item on the ground if you wanna reattach or use or litter.
    AudioEngine::getInstance().playSoundFromList({
        "res/sfx/wip_Pl_IceBreaking00.wav",
        "res/sfx/wip_Pl_IceBreaking01.wav",
        "res/sfx/wip_Pl_IceBreaking02.wav",
    });
    textmesh::regenerateTextMeshMesh(d->uiMaterializeItem, getUIMaterializeItemText(d));
}

void initRootWaza(Player_XData::AttackWaza& waza)
{
    // @HARDCODE: this kind of data would ideally be written in some kind of scripting file fyi.
    waza.animationTrigger = "goto_combat_prepause";
    waza.staminaCost = 25;
    waza.duration = 0.25f;
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { -1.0f, 0.0f, 0.0f },
        .nodeEnd2 = { -5.0f, 0.0f, 0.0f },
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { -0.924f, 0.0f, 0.383f },
        .nodeEnd2 = { -4.619f, 0.0f, 1.913f },
        .executeAtTime = 0.125f,
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { -0.707f, 0.0f, 0.707f },
        .nodeEnd2 = { -3.536f, 0.0f, 3.536f },
        .executeAtTime = 0.25f,
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { -0.383f, 0.0f, 0.924f },
        .nodeEnd2 = { -1.913f, 0.0f, 4.619f },
        .executeAtTime = 0.375f,
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { 0.0f, 0.0f, 1.0f },
        .nodeEnd2 = { 0.0f, 0.0f, 5.0f },
        .executeAtTime = 0.5f,
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { 0.383f, 0.0f, 0.924f },
        .nodeEnd2 = { 1.913f, 0.0f, 4.619f },
        .executeAtTime = 0.625f,
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { 0.707f, 0.0f, 0.707f },
        .nodeEnd2 = { 3.536f, 0.0f, 3.536f },
        .executeAtTime = 0.75f,
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { 0.924f, 0.0f, 0.383f },
        .nodeEnd2 = { 4.619f, 0.0f, 1.913f },
        .executeAtTime = 0.875f,
    });
    waza.hitscanNodes.push_back({
        .nodeEnd1 = { 1.0f, 0.0f, 0.0f },
        .nodeEnd2 = { 5.0f, 0.0f, 0.0f },
        .executeAtTime = 1.0f,
    });
    for (auto& hsn : waza.hitscanNodes)
        hsn.executeAtTime *= waza.duration;  // @DEBUG.. I think. It scales the execution time.
}

void processWeaponAttackInput(Player_XData* d)
{
    Player_XData::AttackWaza* nextWaza = nullptr;
    bool attackFailed = false;
    int16_t staminaCost;

    if (d->currentWaza == nullptr)
    {
        // By default start at the root waza.
        nextWaza = &d->rootWaza;
    }
    else
    {
        // Check if input chains into another attack.
        bool doChain = false;
        for (auto& chain : d->currentWaza->chains)
            if (d->wazaTimer >= chain.inputTimeWindowStart &&
                d->wazaTimer < chain.inputTimeWindowEnd)
            {
                doChain = true;
                nextWaza = chain.nextAttack;
                break;
            }

        if (!doChain)
        {
            attackFailed = true;  // No chain matched the timing: attack failure by bad rhythm.
            staminaCost = 25;     // Bad rhythm penalty.
        }
    }

    // Check if stamina is sufficient.
    if (!attackFailed)
    {
        staminaCost = nextWaza->staminaCost;
        if (staminaCost > d->staminaData.currentStamina)
            attackFailed = true;
    }
    
    // Collect stamina cost
    changeStamina(d, -staminaCost);

    // Execute attack
    if (attackFailed)
    {
        AudioEngine::getInstance().playSound("res/sfx/wip_SE_S_HP_GAUGE_DOWN.wav");
        d->attackTwitchAngle = (float_t)std::rand() / (RAND_MAX / 2.0f) > 0.5f ? glm_rad(2.0f) : glm_rad(-2.0f);  // The most you could do was a twitch (attack failure).
    }
    else
    {
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_MM_Link_Attack1.wav",
            "res/sfx/wip_MM_Link_Attack2.wav",
            "res/sfx/wip_MM_Link_Attack3.wav",
            "res/sfx/wip_MM_Link_Attack4.wav",
            //"res/sfx/wip_hollow_knight_sfx/hero_nail_art_great_slash.wav",
        });

        // Kick off new waza with a clear state.
        d->currentWaza = nextWaza;
        d->wazaTimer = 0.0f;
        d->wazaCurrentHitScanIdx = 1;  // 0th hitscan node is ignored.

        d->characterRenderObj->animator->setTrigger(d->currentWaza->animationTrigger);
    }
}

void processWazaUpdate(Player_XData* d, EntityManager* em, const float_t& physicsDeltaTime)
{
    d->wazaTimer += physicsDeltaTime;
    size_t hitscanLayer = physengine::getCollisionLayer("HitscanInteractible");
    assert(d->currentWaza->hitscanNodes.size() >= 2);

    bool playWazaHitSfx = false;

    // Execute all hitscans that need to be executed in the timeline.
    while (d->wazaCurrentHitScanIdx < d->currentWaza->hitscanNodes.size() &&
        d->wazaTimer >= d->currentWaza->hitscanNodes[d->wazaCurrentHitScanIdx].executeAtTime)
    {
        auto& node     = d->currentWaza->hitscanNodes[d->wazaCurrentHitScanIdx];
        auto& nodePrev = d->currentWaza->hitscanNodes[d->wazaCurrentHitScanIdx - 1];

        vec3 translation;
        glm_vec3_add(d->position, vec3{ 0.0f, d->cpd->radius + d->cpd->height * 0.5f, 0.0f }, translation);
        mat4 rotation;
        glm_euler_zyx(vec3{ 0.0f, d->facingDirection, 0.0f }, rotation);

        for (uint32_t s = 0; s <= d->currentWaza->numHitscanSamples; s++)
        {
            float_t t = (float_t)s / (float_t)d->currentWaza->numHitscanSamples;
            vec3 pt1, pt2;
            glm_vec3_lerp(node.nodeEnd1, node.nodeEnd2, t, pt1);
            glm_vec3_lerp(nodePrev.nodeEnd1, nodePrev.nodeEnd2, t, pt2);

            glm_mat4_mulv3(rotation, pt1, 0.0f, pt1);
            glm_mat4_mulv3(rotation, pt2, 0.0f, pt2);
            glm_vec3_add(pt1, translation, pt1);
            glm_vec3_add(pt2, translation, pt2);

            std::vector<std::string> hitGuids;
            if (physengine::lineSegmentCast(pt1, pt2, hitscanLayer, true, hitGuids))
            {
                // @TODO: @INCOMPLETE: @HARDCODE: dummy values
                float_t attackLvl = 4;

                // Successful hitscan!
                for (auto& guid : hitGuids)
                {
                    DataSerializer ds;
                    ds.dumpString("msg_hitscan_hit");
                    ds.dumpFloat(attackLvl);
                    DataSerialized dsd = ds.getSerializedData();
                    em->sendMessage(guid, dsd);
                }

                playWazaHitSfx = true;
                break;
            }
        }
        d->wazaCurrentHitScanIdx++;
    }

    // Play sound if an attack waza landed.
    if (playWazaHitSfx)
        AudioEngine::getInstance().playSound("res/sfx/wip_EnemyHit_Critical.wav");

    // End waza if duration has passed.
    if (d->wazaTimer >= d->currentWaza->duration)
        d->currentWaza = nullptr;
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

    _data->staminaData.currentStamina = _data->staminaData.maxStamina;

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

    _data->cpd = physengine::createCapsule(getGUID(), 0.5f, 1.0f);  // Total height is 2, but r*2 is subtracted to get the capsule height (i.e. the line segment length that the capsule rides along)
    glm_vec3_copy(_data->position, _data->cpd->basePosition);

    globalState::playerGUID = getGUID();
    globalState::playerPositionRef = &_data->cpd->basePosition;

    _data->uiMaterializeItem = textmesh::createAndRegisterTextMesh("defaultFont", textmesh::RIGHT, textmesh::BOTTOM, getUIMaterializeItemText(_data));
    _data->uiMaterializeItem->isPositionScreenspace = true;
    glm_vec3_copy(vec3{ 925.0f, -510.0f, 0.0f }, _data->uiMaterializeItem->renderPosition);
    _data->uiMaterializeItem->scale = 25.0f;

    _data->uiStamina = textmesh::createAndRegisterTextMesh("defaultFont", textmesh::LEFT, textmesh::MID, getStaminaText(_data));
    _data->uiStamina->isPositionScreenspace = true;
    glm_vec3_copy(vec3{ 25.0f, -135.0f, 0.0f }, _data->uiStamina->renderPosition);
    _data->uiStamina->scale = 25.0f;

    initRootWaza(_data->rootWaza);
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

    if (_data->currentWaza == nullptr)
    {
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
    }
    else
    {
        //
        // Update waza performance
        //
        glm_vec3_zero(_data->worldSpaceInput);  // Filter movement to put out the waza.
        _data->inputFlagJump = false;
        _data->inputFlagRelease = false;  // @NOTE: @TODO: Idk if this is appropriate or wanted behavior.

        processWazaUpdate(_data, _em, physicsDeltaTime);
    }


    //
    // Process input flags
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

    //
    // Update stamina gauge
    //
    if (_data->staminaData.refillTimer > 0.0f)
        _data->staminaData.refillTimer -= physicsDeltaTime;
    else if (_data->staminaData.currentStamina != _data->staminaData.maxStamina)
        changeStamina(_data, _data->staminaData.refillRate * physicsDeltaTime);

    if (_data->staminaData.changedTimer > 0.0f)
    {
        _data->uiStamina->excludeFromBulkRender = false;
        _data->staminaData.changedTimer -= physicsDeltaTime;
    }
    else
        _data->uiStamina->excludeFromBulkRender = true;


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
    ImGui::DragFloat3("uiStamina->renderPosition", _data->uiStamina->renderPosition);
}
