#include "Character.h"

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
#include "StringHelper.h"
#include "HarvestableItem.h"
#include "ScannableItem.h"
#include "Debug.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#ifdef _DEVELOP
#include "HotswapResources.h"
#endif


std::string CHARACTER_TYPE_PLAYER = "PLAYER";
std::string CHARACTER_TYPE_NPC = "NPC";

struct Character_XData
{
    std::string characterType = CHARACTER_TYPE_PLAYER;

    RenderObjectManager*     rom;
    Camera*                  camera;
    RenderObject*            characterRenderObj;
    RenderObject*            handleRenderObj;
    RenderObject*            weaponRenderObj;
    std::string              weaponAttachmentJointName;

    physengine::CapsulePhysicsData* cpd;

    textmesh::TextMesh* uiMaterializeItem;  // @NOTE: This is a debug ui thing. In the real thing, I'd really want there to be in the bottom right the ancient weapon handle pointing vertically with the materializing item in a wireframe with a mysterious blue hue at the end of the handle, and when the item does get materialized, it becomes a rendered version.
    globalState::ScannableItemOption* materializedItem = nullptr;
    int32_t currentWeaponDurability;

    textmesh::TextMesh* uiStamina;
    struct StaminaData
    {
        float_t currentStamina;
        int16_t maxStamina = 100;
        float_t refillTime = 0.5f;  // Wait this time before starting to refill stamina.
        float_t refillTimer = 0.0f;
        float_t changedTime = 0.5f;  // Wait this time before disappearing after a stamina change occurred.
        float_t changedTimer = 0.0f;
        float_t refillRate = 50.0f;

        float_t depletionOverflow = 0.0f;
        float_t doRemove1HealthThreshold = 5.0f;
    } staminaData;

    struct AttackWaza
    {
        std::string wazaName = "";

        enum class WazaInput
        {
            NONE = 0,
            PRESS_X,       PRESS_A,       PRESS_X_A,
            RELEASE_X,     RELEASE_A,     RELEASE_X_A,
        };

        struct EntranceInputParams
        {
            bool enabled = false;
            std::string weaponType = "NULL";  // Valid options: twohanded, bow, dual, spear (NULL means there is no entrance)
            std::string movementState = "NULL";  // Valid options: grounded, midair, upsidedown (NULL means there is no entrance)
            std::string inputName = "NULL";  // Valid options: press_(x/a/x_a), hold_(x/a/x_a), release_(x/a/x_a), doubleclick_(x/a/x_a), doublehold_(x/a/x_a)
            WazaInput input;
            // @NOTE: for `input`, there are some inputs that collide with each other. You will have to worry about this depending on what an available chain's inputs are and which entrances are available (i.e. if you're in an interruptable state at the moment).
            //        The inputs that collide are:
            //            press_@, doubleclick_@, doublehold_@    // Bc doubleclick and doublehold each of them require a `press_@` at the beginning.
            //        These don't collide because:
            //            press_@, hold_@                         // Bc press requires to click and release in a short amount of time. Hold requires to click and hold @ for an amount of time.
            //            press_@, release_@                      // Bc release will trigger the moment @ is not pressed, whereas press will require >=1 tick of @ not pressed, then >=1 <time_for_hold_to_activate tick(s) of @ pressed, then will trigger the moment @ is not pressed again.
            //
            // @REPLY: this point is moot. Will not do this waza input system anymore.
        } entranceInputParams;

        std::string animationState;
        int16_t staminaCost = 0;
        int16_t staminaCostHold = 0;
        int16_t staminaCostHoldTimeFrom = -1;
        int16_t staminaCostHoldTimeTo = -1;
        int16_t duration = -1;
        bool holdMidair = false;
        int16_t holdMidairTimeFrom = -1;
        int16_t holdMidairTimeTo = -1;
        float_t gravityMultiplier = 1.0f;

        struct VelocityDecaySetting
        {
            float_t velocityDecay;
            int16_t executeAtTime = 0;
        };
        std::vector<VelocityDecaySetting> velocityDecaySettings;

        struct VelocitySetting
        {
            vec3 velocity;
            int16_t executeAtTime = 0;
        };
        std::vector<VelocitySetting> velocitySettings;
        
        struct HitscanFlowNode
        {
            // These ends create a line where `numHitscanSamples` number of points traverse.
            // These points are connected to the previous node's ends' traversed lines to create
            // the hitscan query lines. Note also that these points are in object space,
            // where { 0, 0, 1 } represents the player's facing forward vector.
            vec3 nodeEnd1, nodeEnd2;
            int16_t executeAtTime = 0;
        };
        uint32_t numHitscanSamples = 5;
        std::vector<HitscanFlowNode> hitscanNodes;  // Each node uses the previous node's data to create the hitscans (the first node is ignored except for using it as prev node data).
        vec3 hitscanLaunchVelocity = GLM_VEC3_ZERO_INIT;  // Non-normalized vec3 of launch velocity of entity that gets hit by the waza.
        vec3 hitscanLaunchRelPosition = GLM_VEC3_ZERO_INIT;  // Position relative to origin of original character to set hit character on first hit.
        bool hitscanLaunchRelPositionIgnoreY = false;  // Flag to not set the Y relative position.

        struct VacuumSuckIn
        {
            bool enabled = false;
            vec3 position = GLM_VEC3_ZERO_INIT;  // Position relative to character to suck in nearby entities.
            float_t radius = 3.0f;
            float_t strength = 1.0f;
        } vacuumSuckIn;

        struct ForceZone
        {
            bool enabled = false;
            vec3 origin = GLM_VEC3_ZERO_INIT;  // Relative position from character origin.
            vec3 bounds = { 1.0f, 1.0f, 1.0f };  // @NOTE: this is an aabb.
            vec3 forceVelocity = { 1.0f, 0.0f, 0.0f };
            int16_t timeFrom = -1;
            int16_t timeTo = -1;
        } forceZone;

        struct Chain
        {
            int16_t inputTimeWindowStart = 0;  // Press the attack button in this window to trigger the chain.
            int16_t inputTimeWindowEnd = 0;
            std::string nextWazaName = "";  // @NOTE: this is just for looking up the correct next action.
            AttackWaza* nextWazaPtr = nullptr;  // Baked data.
            std::string inputName = "NULL";  // REQUIRED: see `EntranceInputParams::input` for list of valid inputs.
            WazaInput input;
        };
        std::vector<Chain> chains;  // Note that you can have different chains depending on your rhythm in the attack.

        std::string onHoldCancelWazaName = "NULL";
        AttackWaza* onHoldCancelWazaPtr = nullptr;

        std::string onDurationPassedWazaName = "NULL";
        AttackWaza* onDurationPassedWazaPtr = nullptr;

        struct IsInterruptable  // Can interrupt by starting another waza.
        {
            bool enabled = false;
            int16_t from = -1;
            int16_t to = -1;
        } interruptable;
    };
    std::vector<AttackWaza> wazaSet;

    AttackWaza* currentWaza = nullptr;
    vec3        prevWazaHitscanNodeEnd1, prevWazaHitscanNodeEnd2;
    float_t     wazaVelocityDecay = 0.0f;
    vec3        wazaVelocity;
    int16_t     wazaTimer = 0;  // Used for timing chains and hitscans.
    float_t     wazaHitTimescale = 1.0f;
    float_t     wazaHitTimescaleOnHit = 0.01f;
    float_t     wazaHitTimescaleReturnToOneSpeed = 500.0f;

    enum PressedState { INVALID = 0, PRESSED, RELEASED, };
    PressedState prevInput_x;
    PressedState prevInput_a;
    PressedState prevInput_xa;

    bool isMidairUpsideDown = false;  // @NOCHECKIN: implement the flipping action!  @REPLY: well, first, figure out how the heck you'll do it first.

    // Waza Editor/Viewer State
    struct AttackWazaEditor
    {
        bool isEditingMode = false;
        bool triggerRecalcWazaCache = false;  // Trigger to do expensive calculations for specific single waza. Only turn on when state changes.
        float_t preEditorAnimatorSpeedMultiplier;

        std::string editingWazaFname;
        std::vector<AttackWaza> editingWazaSet;
        size_t wazaIndex;
        int16_t currentTick, minTick, maxTick;  // @NOTE: bounds are inclusive.

        vec2 bladeDistanceStartEnd = { 1.0f, 5.0f };
        std::string bladeBoneName = "Hand Attachment";
        std::string bladeBoneName_dirty = bladeBoneName;

        std::string hitscanLaunchVelocityExportString = "";
        std::string hitscanSetExportString = "";
        std::string vacuumSuckInExportString = "";
        std::string forceZoneExportString = "";

        bool triggerBakeHitscans = false;
        int16_t bakeHitscanStartTick = -1, bakeHitscanEndTick = -1;

        bool triggerRecalcHitscanLaunchVelocityCache = false;
        std::vector<vec3s> hitscanLaunchVelocitySimCache;

        bool triggerRecalcSelfVelocitySimCache = false;
        std::vector<vec3s> selfVelocitySimCache;

        int32_t hitscanLaunchAndSelfVelocityAwaseIndex = 0;
    } attackWazaEditor;

    // Notifications
    struct Notification
    {
        float_t showMessageTime = 2.0f;
        float_t showMessageTimer = 0.0f;
        textmesh::TextMesh* message = nullptr;
    } notification;

    vec3 worldSpaceInput = GLM_VEC3_ZERO_INIT;
    float_t gravityForce = 0.0f;
#ifdef _DEVELOP
    bool    disableInput = false;  // @DEBUG for level editor
#endif
    bool    inputFlagJump = false;
    bool    inputFlagAttack = false;
    bool    inputFlagRelease = false;
    float_t attackTwitchAngle = 0.0f;
    float_t attackTwitchAngleReturnSpeed = 3.0f;
    bool    prevIsGrounded = false;
    vec3    prevGroundNormal = GLM_VEC3_ZERO_INIT;

    vec3    launchVelocity;
    vec3    launchSetPosition;
    bool    launchRelPosIgnoreY;
    bool    triggerLaunchVelocity = false;

    vec3    suckInVelocity;
    vec3    suckInTargetPosition;
    bool    triggerSuckIn = false;

    vec3    forceZoneVelocity;
    bool    triggerApplyForceZone = false;
    bool    inGettingPressedAnim = false;

    bool    prevIsMoving = false;
    bool    prevPrevIsGrounded = false;
    bool    prevPerformedJump = false;

    float_t inputMaxXZSpeed = 7.5f;
    float_t midairXZAcceleration = 1.0f;
    float_t midairXZDeceleration = 0.25f;
    float_t knockedbackGroundedXZDeceleration = 0.5f;
    float_t recoveryGroundedXZDeceleration = 0.75f;
    vec3    prevCPDBasePosition;

    std::vector<int32_t> auraSfxChannelIds;

    // Tweak Props
    vec3 position;
    float_t facingDirection = 0.0f;
    float_t modelSize = 0.3f;
    
    int32_t health = 100;
    float_t iframesTime = 0.15f;
    float_t iframesTimer = 0.0f;

    enum KnockbackStage { NONE, RECOVERY, KNOCKED_UP };
    KnockbackStage knockbackMode = KnockbackStage::NONE;
    float_t        knockedbackTime = 0.35f;
    float_t        knockedbackTimer = 0.0f;

    std::vector<size_t> harvestableItemsIdsToSpawnAfterDeath;
    std::vector<size_t> scannableItemsIdsToSpawnAfterDeath;
};

void processOutOfHealth(EntityManager* em, Entity* e, Character_XData* d)
{
    // Drop off items and then destroy self.
    for (size_t id : d->harvestableItemsIdsToSpawnAfterDeath)
    {
        DataSerializer ds;
        ds.dumpString(e->getGUID());  // Use this guid to force a guid recalculation.
        ds.dumpVec3(d->position);
        float_t hii = (float_t)id;
        ds.dumpFloat(hii);
        DataSerialized dsd = ds.getSerializedData();
        new HarvestableItem(em, d->rom, &dsd);
    }
    for (size_t id : d->scannableItemsIdsToSpawnAfterDeath)
    {
        DataSerializer ds;
        ds.dumpString(e->getGUID());  // Use this guid to force a guid recalculation.
        ds.dumpVec3(d->position);
        float_t hii = (float_t)id;
        ds.dumpFloat(hii);
        DataSerialized dsd = ds.getSerializedData();
        new ScannableItem(em, d->rom, &dsd);
    }
    em->destroyEntity(e);
}

void pushPlayerNotification(const std::string& message, Character_XData* d)
{
    AudioEngine::getInstance().playSound("res/sfx/wip_bonk.ogg");
    d->notification.showMessageTimer = d->notification.showMessageTime;

    // Lazyload the message textmesh. (@NOTE: no multithreading so no locks required)
    if (d->notification.message == nullptr)
    {
        d->notification.message = textmesh::createAndRegisterTextMesh("defaultFont", textmesh::CENTER, textmesh::MID, message);
        d->notification.message->isPositionScreenspace = true;
        glm_vec3_copy(vec3{ 0.0f, 250.0f, 0.0f }, d->notification.message->renderPosition);
        d->notification.message->scale = 25.0f;
    }
    else
        textmesh::regenerateTextMeshMesh(d->notification.message, message);
}

std::string getUIMaterializeItemText(Character_XData* d)
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

std::string getStaminaText(Character_XData* d)
{
    return "Stamina: " + std::to_string((int32_t)std::round(d->staminaData.currentStamina)) + "/" + std::to_string(d->staminaData.maxStamina);
}

void changeStamina(Character_XData* d, float_t amount, bool allowDepletionOverflow)
{
    d->staminaData.currentStamina += amount;
    if (allowDepletionOverflow && d->staminaData.currentStamina < 0.0f)
    {
        // If character gets overexerted, `depletionOverflow` gets too large, then character will start losing health.
        d->staminaData.depletionOverflow += -d->staminaData.currentStamina;
        while (d->staminaData.depletionOverflow >= d->staminaData.doRemove1HealthThreshold)
        {
            d->staminaData.depletionOverflow -= d->staminaData.doRemove1HealthThreshold;
            globalState::savedPlayerHealth -= 1;
            AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_OOT_YoungLink_Hurt1.wav",
                "res/sfx/wip_OOT_YoungLink_Hurt2.wav",
                "res/sfx/wip_OOT_YoungLink_Hurt3.wav",
            });
        }
    }

    d->staminaData.currentStamina = std::clamp(d->staminaData.currentStamina, 0.0f, (float_t)d->staminaData.maxStamina);

    if (amount < 0)
        d->staminaData.refillTimer = d->staminaData.refillTime;
    d->staminaData.changedTimer = d->staminaData.changedTime;

    textmesh::regenerateTextMeshMesh(d->uiStamina, getStaminaText(d));
}

void processAttack(Character_XData* d)
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
                d->currentWeaponDurability = d->materializedItem->weaponStats.durability;  // @NOTE: non-weapons will have garbage set as their durability. Just ignore.
                d->characterRenderObj->animator->setTrigger("goto_draw_weapon");
                d->characterRenderObj->animator->setTrigger("goto_mcm_draw_weapon");
            }
            else
            {
                pushPlayerNotification("Not enough materials for materialization.", d);
            }
        }
        else
        {
            pushPlayerNotification("No item is selected to materialize.", d);
        }
    }
    else if (d->staminaData.currentStamina > 0.0f)
    {
        // Attempt to use materialized item.
        switch (d->materializedItem->type)
        {
            case globalState::WEAPON:
                // Do nothing. This section is being handled by `processWazaInput` bc the inputs are so complex.
                break;

            case globalState::FOOD:
            {
                // Attempt to eat.
                globalState::savedPlayerHealth += 5;
                d->materializedItem = nullptr;  // Ate the item off the handle.
                d->weaponRenderObj->renderLayer = RenderLayer::INVISIBLE;
                AudioEngine::getInstance().playSound("res/sfx/wip_Pl_Eating_S00.wav");
                AudioEngine::getInstance().playSound("res/sfx/wip_Sys_ExtraHeartUp_01.wav");
                d->characterRenderObj->animator->setTrigger("goto_sheath_weapon");  // @TODO: figure out how to prevent ice breaking sfx in hokasu event.  @REPLY: you need to make another animation that has character eating the item and then put away the weapon, and then goto that animation instead of the "break off" animation.
                d->characterRenderObj->animator->setTrigger("goto_mcm_sheath_weapon");
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

void processRelease(Character_XData* d)
{
    if (d->materializedItem == nullptr)
    {
        // Cycle thru the available materializable items.
        if (globalState::selectNextCanMaterializeScannableItemId())
            AudioEngine::getInstance().playSound("res/sfx/wip_SYS_AppHome_Slide.wav");
        textmesh::regenerateTextMeshMesh(d->uiMaterializeItem, getUIMaterializeItemText(d));
    }
    else
    {
        // Release the item off the handle.
        d->materializedItem = nullptr;
        d->characterRenderObj->animator->setTrigger("goto_sheath_weapon");
        d->characterRenderObj->animator->setTrigger("goto_mcm_sheath_weapon");
    }
    textmesh::regenerateTextMeshMesh(d->uiMaterializeItem, getUIMaterializeItemText(d));
}

void parseVec3CommaSeparated(const std::string& vec3Str, vec3& outVec3)
{
    std::string strCopy = vec3Str;
    std::string::size_type sz;
    outVec3[0] = std::stof(strCopy, &sz);    strCopy = strCopy.substr(sz + 1);
    outVec3[1] = std::stof(strCopy, &sz);    strCopy = strCopy.substr(sz + 1);
    outVec3[2] = std::stof(strCopy);
}

void loadDataFromLine(Character_XData::AttackWaza& newWaza, const std::string& command, const std::vector<std::string>& params)
{
    if (command == "entrance")
    {
        newWaza.entranceInputParams.enabled = true;
        newWaza.entranceInputParams.weaponType = params[0];
        newWaza.entranceInputParams.movementState = params[1];
        newWaza.entranceInputParams.inputName = params[2];
    }
    else if (command == "animation_state")
    {
        newWaza.animationState = params[0];
    }
    else if (command == "stamina_cost")
    {
        newWaza.staminaCost = std::stoi(params[0]);
    }
    else if (command == "stamina_cost_hold")
    {
        newWaza.staminaCostHold = std::stoi(params[0]);
        if (params.size() >= 2)
            newWaza.staminaCostHoldTimeFrom = std::stoi(params[1]);
        if (params.size() >= 3)
            newWaza.staminaCostHoldTimeTo = std::stoi(params[2]);
    }
    else if (command == "duration")
    {
        newWaza.duration = std::stoi(params[0]);
    }
    else if (command == "hold_midair")
    {
        newWaza.holdMidair = true;
        if (params.size() >= 2)
        {
            newWaza.holdMidairTimeFrom = std::stoi(params[0]);
            newWaza.holdMidairTimeTo = std::stoi(params[1]);
        }
    }
    else if (command == "gravity_multiplier")
    {
        newWaza.gravityMultiplier = std::stof(params[0]);
    }
    else if (command == "velocity_decay")
    {
        Character_XData::AttackWaza::VelocityDecaySetting newVelocityDecaySetting;
        newVelocityDecaySetting.velocityDecay = std::stof(params[0]);
        newVelocityDecaySetting.executeAtTime = std::stoi(params[1]);
        newWaza.velocityDecaySettings.push_back(newVelocityDecaySetting);
    }
    else if (command == "velocity")
    {
        Character_XData::AttackWaza::VelocitySetting newVelocitySetting;
        vec3 velo;
        parseVec3CommaSeparated(params[0], velo);
        glm_vec3_copy(velo, newVelocitySetting.velocity);
        newVelocitySetting.executeAtTime = std::stoi(params[1]);
        newWaza.velocitySettings.push_back(newVelocitySetting);
    }
    else if (command == "hitscan")
    {
        Character_XData::AttackWaza::HitscanFlowNode newHitscanNode;
        vec3 end1, end2;
        parseVec3CommaSeparated(params[0], end1);
        parseVec3CommaSeparated(params[1], end2);
        glm_vec3_copy(end1, newHitscanNode.nodeEnd1);
        glm_vec3_copy(end2, newHitscanNode.nodeEnd2);
        if (params.size() >= 3)
            newHitscanNode.executeAtTime = std::stoi(params[2]);
        newWaza.hitscanNodes.push_back(newHitscanNode);
    }
    else if (command == "hs_launch_velocity")
    {
        parseVec3CommaSeparated(params[0], newWaza.hitscanLaunchVelocity);
    }
    else if (command == "hs_rel_position")
    {
        parseVec3CommaSeparated(params[0], newWaza.hitscanLaunchRelPosition);
        if (params.size() >= 2 && params[1] == "ignore_y")
            newWaza.hitscanLaunchRelPositionIgnoreY = true;
    }
    else if (command == "vacuum_suck_in")
    {
        newWaza.vacuumSuckIn.enabled = true;
        parseVec3CommaSeparated(params[0], newWaza.vacuumSuckIn.position);
        newWaza.vacuumSuckIn.radius = std::stoi(params[1]);
        newWaza.vacuumSuckIn.strength = std::stoi(params[2]);
    }
    else if (command == "force_zone")
    {
        newWaza.forceZone.enabled = true;
        parseVec3CommaSeparated(params[0], newWaza.forceZone.origin);
        parseVec3CommaSeparated(params[1], newWaza.forceZone.bounds);
        parseVec3CommaSeparated(params[2], newWaza.forceZone.forceVelocity);
        newWaza.forceZone.timeFrom = std::stoi(params[3]);
        newWaza.forceZone.timeTo = std::stoi(params[4]);
    }
    else if (command == "chain")
    {
        Character_XData::AttackWaza::Chain newChain;
        newChain.nextWazaName = params[0];
        newChain.inputTimeWindowStart = std::stoi(params[1]);
        newChain.inputTimeWindowEnd = std::stoi(params[2]);
        newChain.inputName = params[3];
        newWaza.chains.push_back(newChain);
    }
    else if (command == "on_hold_cancel")
    {
        newWaza.onHoldCancelWazaName = params[0];
    }
    else if (command == "on_duration_passed")
    {
        newWaza.onDurationPassedWazaName = params[0];
    }
    else if (command == "interruptable")
    {
        newWaza.interruptable.enabled = true;
        if (params.size() >= 1)
            newWaza.interruptable.from = std::stoi(params[0]);
        if (params.size() >= 2)
            newWaza.interruptable.to = std::stoi(params[1]);
    }
    else
    {
        // ERROR
        std::cerr << "[WAZA LOADING]" << std::endl
            << "ERROR: Unknown command token: " << command << std::endl;
    }
}

Character_XData::AttackWaza* getWazaPtrFromName(std::vector<Character_XData::AttackWaza>& wazas, const std::string& wazaName)
{
    if (wazaName == "NULL")  // Special case.
        return nullptr;

    for (Character_XData::AttackWaza& waza : wazas)
    {
        if (waza.wazaName == wazaName)
            return &waza;
    }

    std::cerr << "[WAZA LOADING]" << std::endl
        << "ERROR: Waza with name \"" << wazaName << "\" was not found (`getWazaPtrFromName`)." << std::endl;
    return nullptr;
}

Character_XData::AttackWaza::WazaInput getInputEnumFromName(const std::string& inputName)
{
    int32_t enumValue = -1;
    int32_t x = -1;
    if (inputName.rfind("press_", 0) == 0)
        x = 0;
    else if (inputName.rfind("release_", 0) == 0)
        x = 1;

    int32_t y = -1;
    std::string suffix = inputName.substr(inputName.find("_") + 1);
    if (suffix == "x")
        y = 0;
    else if (suffix == "a")
        y = 1;
    else if (suffix == "x_a")
        y = 2;
    
    enumValue = 3 * x + y + 1;  // @COPYPASTA
    
    if (enumValue < 1)
    {
        std::cerr << "[WAZA LOADING]" << std::endl
            << "ERROR: Waza input \"" << inputName << "\" was not found (`getInputEnumFromName`)." << std::endl;
        return Character_XData::AttackWaza::WazaInput(0);
    }

    return Character_XData::AttackWaza::WazaInput(enumValue);
}

void initWazaSetFromFile(std::vector<Character_XData::AttackWaza>& wazas, const std::string& fname)
{
    std::ifstream wazaFile(fname);
    if (!wazaFile.is_open())
    {
        std::cerr << "[WAZA LOADING]" << std::endl
            << "WARNING: file \"" << fname << "\" not found, thus could not load the waza action commands." << std::endl;
        return;
    }

    //
    // Parse the commands
    //
    Character_XData::AttackWaza newWaza;
    std::string line;
    for (size_t lineNum = 1; std::getline(wazaFile, line); lineNum++)  // @COPYPASTA with SceneManagement.cpp
    {
        // Prep line data
        std::string originalLine = line;

        size_t found = line.find('#');
        if (found != std::string::npos)
        {
            line = line.substr(0, found);
        }

        trim(line);
        if (line.empty())
            continue;

        // Package finished state
        if (line[0] == ':')
        {
            if (!newWaza.wazaName.empty())
            {
                wazas.push_back(newWaza);
                newWaza = Character_XData::AttackWaza();
            }
        }

        // Process line
        if (line[0] == ':')
        {
            line = line.substr(1);  // Cut out colon
            trim(line);

            newWaza.wazaName = line;
        }
        else if (!newWaza.wazaName.empty())
        {
            std::string lineCommand = line.substr(0, line.find(' '));
            trim(lineCommand);

            std::string lineParams = line.substr(lineCommand.length());
            trim(lineParams);
            std::vector<std::string> paramsParsed;
            while (true)
            {
                size_t nextWS;
                if ((nextWS = lineParams.find(' ')) == std::string::npos)
                {
                    // This is a single param. End of the list.
                    paramsParsed.push_back(lineParams);
                    break;
                }
                else
                {
                    // Break off the param and add the first one to the list.
                    std::string param = lineParams.substr(0, nextWS);
                    trim(param);
                    paramsParsed.push_back(param);

                    lineParams = lineParams.substr(nextWS);
                    trim(lineParams);
                }
            }

            loadDataFromLine(newWaza, lineCommand, paramsParsed);
        }
        else
        {
            // ERROR
            std::cerr << "[WAZA LOADING]" << std::endl
                << "ERROR (line " << lineNum << ") (file: " << fname << "): Headless data" << std::endl
                << "   Trimmed line: " << line << std::endl
                << "  Original line: " << line << std::endl;
        }
    }

    // Package finished state
    // @COPYPASTA
    if (!newWaza.wazaName.empty())
    {
        wazas.push_back(newWaza);
        newWaza = Character_XData::AttackWaza();
    }

    //
    // Bake pointers into string references.
    //
    for (Character_XData::AttackWaza& waza : wazas)
    {
        if (waza.wazaName == "NULL")
        {
            std::cerr << "[WAZA LOADING]" << std::endl
                << "ERROR: You can't name a waza state \"NULL\"... it's a keyword!!! Aborting." << std::endl;
            break;
        }

        if (waza.entranceInputParams.inputName != "NULL")
            waza.entranceInputParams.input = getInputEnumFromName(waza.entranceInputParams.inputName);

        for (Character_XData::AttackWaza::Chain& chain : waza.chains)
        {
            chain.nextWazaPtr = getWazaPtrFromName(wazas, chain.nextWazaName);
            chain.input = getInputEnumFromName(chain.inputName);
        }
        waza.onHoldCancelWazaPtr = getWazaPtrFromName(wazas, waza.onHoldCancelWazaName);
        waza.onDurationPassedWazaPtr = getWazaPtrFromName(wazas, waza.onDurationPassedWazaName);
    }
}

// @TODO: delete this once not needed. Well, it's not needed rn bc it's commented out, but once the knowledge isn't needed anymore, delete this.  -Timo 2023/09/19
// void processWeaponAttackInput(Character_XData* d)
// {
//     Character_XData::AttackWaza* nextWaza = nullptr;
//     bool attackFailed = false;
//     int16_t staminaCost;

//     if (d->currentWaza == nullptr)
//     {
//         // By default start at the root waza.
//         // @NOCHECKIN: @FIXME: collect the inputs natively right here instead of having to wait for LMB input or whatever to trigger this function.
//         // if (input::keyAuraPressed)
//         //     nextWaza = &d->airWazaSet[0];
//         // else
//         //     nextWaza = &d->defaultWazaSet[0];
//     }
//     else
//     {
//         // Check if input chains into another attack.
//         bool doChain = false;
//         for (auto& chain : d->currentWaza->chains)
//             if (d->wazaTimer >= chain.inputTimeWindowStart &&
//                 d->wazaTimer <= chain.inputTimeWindowEnd)
//             {
//                 doChain = true;
//                 nextWaza = chain.nextWazaPtr;
//                 break;
//             }

//         if (!doChain)
//         {
//             attackFailed = true;  // No chain matched the timing: attack failure by bad rhythm.
//             staminaCost = 25;     // Bad rhythm penalty.
//         }
//     }

//     // Check if stamina is sufficient.
//     if (!attackFailed)
//     {
//         staminaCost = nextWaza->staminaCost;
//         if (staminaCost > d->staminaData.currentStamina)
//             attackFailed = true;
//     }
    
//     // Collect stamina cost
//     changeStamina(d, -staminaCost);

//     // Execute attack
//     if (attackFailed)
//     {
//         AudioEngine::getInstance().playSound("res/sfx/wip_SE_S_HP_GAUGE_DOWN.wav");
//         d->attackTwitchAngle = (float_t)std::rand() / (RAND_MAX / 2.0f) > 0.5f ? glm_rad(2.0f) : glm_rad(-2.0f);  // The most you could do was a twitch (attack failure).
//     }
//     else
//     {
//         AudioEngine::getInstance().playSoundFromList({
//             "res/sfx/wip_MM_Link_Attack1.wav",
//             "res/sfx/wip_MM_Link_Attack2.wav",
//             "res/sfx/wip_MM_Link_Attack3.wav",
//             "res/sfx/wip_MM_Link_Attack4.wav",
//             //"res/sfx/wip_hollow_knight_sfx/hero_nail_art_great_slash.wav",
//         });

//         // Kick off new waza with a clear state.
//         d->currentWaza = nextWaza;
//         d->wazaVelocityDecay = 0.0f;
//         glm_vec3_copy((d->currentWaza != nullptr && d->currentWaza->velocitySettings.size() > 0 && d->currentWaza->velocitySettings[0].executeAtTime == 0) ? d->currentWaza->velocitySettings[0].velocity : vec3{ 0.0f, 0.0f, 0.0f }, d->wazaVelocity);  // @NOTE: this doesn't work if the executeAtTime's aren't sorted asc.
//         d->wazaTimer = 0;
//         d->characterRenderObj->animator->setState(d->currentWaza->animationState);
//         d->characterRenderObj->animator->setMask("MaskCombatMode", (d->currentWaza == nullptr));
//     }
// }

//////////////// Character_XData::AttackWaza::WazaInputType processInputForKey(Character_XData::PressedState currentInput, uint8_t ticksToHold, uint8_t ticksToClearState, uint8_t invalidTicksToClearState, Character_XData::GestureInputState& inoutIS)
//////////////// {
////////////////     Character_XData::AttackWaza::WazaInputType wit = Character_XData::AttackWaza::WazaInputType::NONE;
//////////////// 
////////////////     // Increment ticks.
////////////////     switch (currentInput)
////////////////     {
////////////////         case Character_XData::PressedState::PRESSED:
////////////////             inoutIS.numPressTicks++;
////////////////             break;
//////////////// 
////////////////         case Character_XData::PressedState::RELEASED:
////////////////             inoutIS.numReleaseTicks++;
////////////////             break;
//////////////// 
////////////////         case Character_XData::PressedState::INVALID:
////////////////             inoutIS.numInvalidTicks++;
////////////////             break;
////////////////     }
//////////////// 
////////////////     // Clamp ticks.
////////////////     inoutIS.numPressTicks = std::min(inoutIS.numPressTicks, (uint8_t)254);
////////////////     inoutIS.numReleaseTicks = std::min(inoutIS.numReleaseTicks, (uint8_t)254);
//////////////// 
////////////////     // Interpret.
////////////////     if (inoutIS.numReleaseTicks >= ticksToClearState ||
////////////////         inoutIS.numInvalidTicks >= invalidTicksToClearState)
////////////////     {
////////////////         inoutIS.numPressTicks = 0;
////////////////         inoutIS.numReleaseTicks = 0;
////////////////         inoutIS.numInvalidTicks = 0;
////////////////         inoutIS.completedPress = false;
////////////////     }
////////////////     else if (currentInput != Character_XData::PressedState::INVALID)
////////////////     {
////////////////         if (currentInput == Character_XData::PressedState::RELEASED && inoutIS.numPressTicks > 0 && inoutIS.numPressTicks <= ticksToHold)
////////////////         {
////////////////             // `press` was executed. (This is really a click.)
////////////////             if (inoutIS.completedPress)
////////////////                 wit = Character_XData::AttackWaza::WazaInputType::DOUBLECLICK;
////////////////             else
////////////////             {
////////////////                 inoutIS.completedPress = true;
////////////////                 wit = Character_XData::AttackWaza::WazaInputType::PRESS;
////////////////             }
////////////////             inoutIS.numPressTicks = 0;
////////////////         }
////////////////         else if (currentInput == Character_XData::PressedState::PRESSED && inoutIS.numPressTicks == ticksToHold)
////////////////         {
////////////////             // `hold` was executed.
////////////////             if (inoutIS.completedPress)
////////////////                 wit = Character_XData::AttackWaza::WazaInputType::DOUBLEHOLD;
////////////////             else
////////////////                 wit = Character_XData::AttackWaza::WazaInputType::HOLD;
////////////////         }
////////////////         else if (currentInput == Character_XData::PressedState::RELEASED && inoutIS.numPressTicks > ticksToHold)
////////////////         {
////////////////             // `release` was executed.
////////////////             wit = Character_XData::AttackWaza::WazaInputType::RELEASE;
////////////////             inoutIS.numPressTicks = 0;
////////////////         }
////////////////     }
//////////////// 
////////////////     // Clear ticks.
////////////////     switch (currentInput)
////////////////     {
////////////////         case Character_XData::PressedState::PRESSED:
////////////////             inoutIS.numReleaseTicks = 0;
////////////////             inoutIS.numInvalidTicks = 0;
////////////////             break;
//////////////// 
////////////////         case Character_XData::PressedState::RELEASED:
////////////////             inoutIS.numPressTicks = 0;
////////////////             inoutIS.numInvalidTicks = 0;
////////////////             break;
//////////////// 
////////////////         case Character_XData::PressedState::INVALID:
////////////////             // Do nothing.
////////////////             break;
////////////////     }
//////////////// 
////////////////     return wit;
//////////////// }

inline Character_XData::PressedState pressedStateSingle(const bool& isPressed)
{
    return isPressed ? Character_XData::PressedState::PRESSED : Character_XData::PressedState::RELEASED;
}

inline Character_XData::PressedState pressedStateCombo(const std::vector<Character_XData::PressedState>& singleStates)
{
    Character_XData::PressedState originalState = singleStates[0];
    for (size_t i = 1; i < singleStates.size(); i++)
        if (originalState != singleStates[i])
            return Character_XData::PressedState::INVALID;
    return originalState;
}

// // @DEBUG
// std::string totootototNOCHECKIN(Character_XData::AttackWaza::WazaInputType wit)
// {
//     switch (wit)
//     {
//         case Character_XData::AttackWaza::WazaInputType::NONE: return "NONE";
//         case Character_XData::AttackWaza::WazaInputType::PRESS: return "PRESS";
//         case Character_XData::AttackWaza::WazaInputType::HOLD: return "HOLD";
//         case Character_XData::AttackWaza::WazaInputType::RELEASE: return "RELEASE";
//         case Character_XData::AttackWaza::WazaInputType::DOUBLECLICK: return "DOUBLECLICK";
//         case Character_XData::AttackWaza::WazaInputType::DOUBLEHOLD: return "DOUBLEHOLD";
//     }

//     return "INVALID (UNKNOWN): " + (int32_t)wit;
// }

// size_t jal = 0;
// ////////

inline Character_XData::AttackWaza::WazaInput inputTypeToWazaInput(int32_t keyType, Character_XData::PressedState inputType)
{
    // @NOTE: this function assumes that `inputType` is >=1.
    return Character_XData::AttackWaza::WazaInput(
        3 * ((int32_t)inputType - 1) + keyType + 1  // @COPYPASTA
    );
}

#define MAX_SIMULTANEOUS_WAZA_INPUTS 8

void processInputForWaza(Character_XData* d, Character_XData::AttackWaza::WazaInput* outWazaInputs, size_t& outNumInputs)
{
    // @NOCHECKIN: I don't like this system for doing the key combinations. `release` events don't work.
    //             Figure out some way for the key combination press, release, and hold, etc. to work.
    //             I think that keeping the 3 states for `currentInput` is good, but making the single input
    //             processing just use the pressed/released states only and not get changed/filtered out.
    //             Then, use the invalid/pressed/released for combination moves. Maybe HOLD/DOUBLEHOLD is really
    //             the only event needing to have priority in combo inputs.  -Timo 2023/09/17
    // @REPLY: I think that this new system is what is needed.  -Timo 2023/09/17
    // @REPLY: I think that the previous new system sucked and now I'm implementing a new system i like (have HWAC file take care of timing).  -Timo 2023/09/22
    Character_XData::PressedState input_x = pressedStateSingle(input::LMBPressed);
    Character_XData::PressedState input_a = pressedStateSingle(input::keyJumpPressed);
    Character_XData::PressedState input_xa = pressedStateCombo({ input_x, input_a });

    // Fill in all the waza inputs. (@NOTE: start with key combinations and check inputs that are highest priority first)
    outNumInputs = 0;
    if (input_xa > Character_XData::PressedState::INVALID &&
        input_xa != d->prevInput_xa)
        outWazaInputs[outNumInputs++] = inputTypeToWazaInput(2, input_xa);
    if (input_x > Character_XData::PressedState::INVALID &&
        input_x != d->prevInput_x)
        outWazaInputs[outNumInputs++] = inputTypeToWazaInput(0, input_x);
    if (input_a > Character_XData::PressedState::INVALID &&
        input_a != d->prevInput_a)
        outWazaInputs[outNumInputs++] = inputTypeToWazaInput(1, input_a);

    d->prevInput_xa = input_xa;
    d->prevInput_x = input_x;
    d->prevInput_a = input_a;
}

struct NextWazaPtr
{
    Character_XData::AttackWaza* nextWaza = nullptr;
    bool set = false;
};

void processWazaInput(Character_XData* d, Character_XData::AttackWaza::WazaInput* wazaInputs, size_t numInputs, NextWazaPtr& inoutNextWaza)
{
    std::string movementState = d->prevIsGrounded ? "grounded" : (d->isMidairUpsideDown ? "upsidedown" : "midair");

    bool isInInterruptableTimeWindow =
        d->currentWaza == nullptr ||
        (d->currentWaza->interruptable.enabled &&
            (d->currentWaza->interruptable.from < 0 || d->wazaTimer >= d->currentWaza->interruptable.from) &&
            (d->currentWaza->interruptable.to < 0 || d->wazaTimer <= d->currentWaza->interruptable.to));

    bool chainIsFromStaminaCostHold =
        d->currentWaza != nullptr &&
        d->currentWaza->staminaCostHold > 0 &&
        (d->currentWaza->staminaCostHoldTimeFrom < 0 || d->wazaTimer >= d->currentWaza->staminaCostHoldTimeFrom) &&
        (d->currentWaza->staminaCostHoldTimeTo < 0 || d->wazaTimer <= d->currentWaza->staminaCostHoldTimeTo);

    // Search for an action to do with the provided inputs.
    bool chainingIntoHoldRelease = false;
    for (size_t i = 0; i < numInputs; i++)
    {
        Character_XData::AttackWaza::WazaInput wazaInput = wazaInputs[i];
        if (wazaInput == Character_XData::AttackWaza::WazaInput::NONE)
        {
            std::cerr << "[PROCESS WAZA INPUT]" << std::endl
                << "ERROR: NONE type waza input came into the function `processWazaInput`" << std::endl;
            continue;
        }

        if (d->currentWaza != nullptr)
        {
            // Search thru chains.
            for (auto& chain : d->currentWaza->chains)
                if (chain.input == wazaInput)
                {
                    bool inChainTimeWindow =
                        (chain.inputTimeWindowStart < 0 || d->wazaTimer >= chain.inputTimeWindowStart) &&
                        (chain.inputTimeWindowEnd < 0 || d->wazaTimer <= chain.inputTimeWindowEnd);
                    if (inChainTimeWindow)
                    {
                        inoutNextWaza.nextWaza = chain.nextWazaPtr;
                        inoutNextWaza.set = true;
                        if (chainIsFromStaminaCostHold)
                            chainingIntoHoldRelease = true;
                        break;
                    }
                    // else if (chain.input == Character_XData::AttackWaza::WazaInput::HOLD_X ||
                    //     chain.input == Character_XData::AttackWaza::WazaInput::HOLD_A ||
                    //     chain.input == Character_XData::AttackWaza::WazaInput::HOLD_X_A)
                    // {
                    //     // The correct chain input for a release was done, however,
                    //     // since it was in the wrong window of timing, it needs to be a hold cancel.
                    //     inoutNextWaza.nextWaza = d->currentWaza->onHoldCancelWazaPtr;
                    //     break;
                    // }
                }
        }

        if (inoutNextWaza.nextWaza == nullptr && isInInterruptableTimeWindow)
        {
            // Search thru entrances.
            // @NOTE: this is lower priority than the chains in the event that a waza is interruptable.
            for (auto& waza : d->wazaSet)
                if (waza.entranceInputParams.enabled &&
                    waza.entranceInputParams.input == wazaInput &&
                    waza.entranceInputParams.weaponType == d->materializedItem->weaponStats.weaponType &&
                    waza.entranceInputParams.movementState == movementState)
                {
                    inoutNextWaza.nextWaza = &waza;
                    inoutNextWaza.set = true;
                    break;
                }
        }

        if (inoutNextWaza.nextWaza != nullptr)
            break;
    }

    // Ignore inputs if no next waza was found.
    // @TODO: decide whether you want the twitch and stamina fail/timing punishment here. I personally feel like since there could be some noise coming thru this function, it wouldn't be good to punish a possibly false-negative combo input.
    if (inoutNextWaza.nextWaza == nullptr)
        return;

    // Calculate needed stamina cost. Attack fails if stamina is not enough.
    bool staminaSufficient = ((float_t)inoutNextWaza.nextWaza->staminaCost <= d->staminaData.currentStamina);
    changeStamina(d, -inoutNextWaza.nextWaza->staminaCost, chainingIntoHoldRelease);  // @NOTE: if a hold release action, then the depletion allows for you to dip into your reserves (health), and then execute the attack despite having no stamina.  -Timo 2023/09/22
    if (!staminaSufficient)
    {
        AudioEngine::getInstance().playSound("res/sfx/wip_SE_S_HP_GAUGE_DOWN.wav");
        d->attackTwitchAngle = (float_t)std::rand() / (RAND_MAX / 2.0f) > 0.5f ? glm_rad(2.0f) : glm_rad(-2.0f);  // The most you could do was a twitch (attack failure).

        if (!chainingIntoHoldRelease)
        {
            inoutNextWaza.nextWaza = nullptr;
            inoutNextWaza.set = true;
        }
    }
}

void processWazaUpdate(Character_XData* d, EntityManager* em, const float_t& physicsDeltaTime, const std::string& myGuid, NextWazaPtr& inoutNextWaza)
{
    //
    // Deplete stamina
    //
    if (d->currentWaza->staminaCostHold > 0 &&
        (d->currentWaza->staminaCostHoldTimeFrom < 0 || d->wazaTimer >= d->currentWaza->staminaCostHoldTimeFrom) &&
        (d->currentWaza->staminaCostHoldTimeTo < 0 || d->wazaTimer <= d->currentWaza->staminaCostHoldTimeTo))
        changeStamina(d, -d->currentWaza->staminaCostHold * physicsDeltaTime, true);

    //
    // Execute all velocity decay settings.
    //
    for (Character_XData::AttackWaza::VelocityDecaySetting& vds : d->currentWaza->velocityDecaySettings)
        if (vds.executeAtTime == d->wazaTimer)
        {
            d->wazaVelocityDecay = vds.velocityDecay;
            break;
        }

    //
    // Execute all velocity settings corresponding to the timer.
    //
    bool setNewVelocity = false;
    for (Character_XData::AttackWaza::VelocitySetting& velocitySetting : d->currentWaza->velocitySettings)
        if (velocitySetting.executeAtTime == d->wazaTimer)
        {
            setNewVelocity = true;
            glm_vec3_copy(velocitySetting.velocity, d->wazaVelocity);
            break;
        }

    if (!setNewVelocity)
    {
        // Apply velocity decay
        float_t newNorm = std::max(0.0f, glm_vec3_norm(d->wazaVelocity) - d->wazaVelocityDecay);
        glm_vec3_scale_as(d->wazaVelocity, newNorm, d->wazaVelocity);
    }

    //
    // Execute all hitscans that need to be executed in the timeline.
    //
    size_t hitscanLayer = physengine::getCollisionLayer("HitscanInteractible");
    assert(d->currentWaza->hitscanNodes.size() != 1);

    bool playWazaHitSfx = false;

    for (size_t i = 1; i < d->currentWaza->hitscanNodes.size(); i++)  // @NOTE: 0th hitscan node is ignored bc it's used to draw the line from 0th to 1st hit scan line.
    {
        if (d->currentWaza->hitscanNodes[i].executeAtTime == d->wazaTimer)
        {
            mat4 rotation;
            glm_euler_zyx(vec3{ 0.0f, d->facingDirection, 0.0f }, rotation);

            auto& node = d->currentWaza->hitscanNodes[i];
            vec3 nodeEnd1WS, nodeEnd2WS;
            glm_mat4_mulv3(rotation, node.nodeEnd1, 0.0f, nodeEnd1WS);
            glm_mat4_mulv3(rotation, node.nodeEnd2, 0.0f, nodeEnd2WS);
            glm_vec3_add(nodeEnd1WS, d->position, nodeEnd1WS);
            glm_vec3_add(nodeEnd2WS, d->position, nodeEnd2WS);

            if (i == 1)
            {
                // Set prev node to 0th flow nodes.
                auto& nodePrev = d->currentWaza->hitscanNodes[i - 1];
                glm_mat4_mulv3(rotation, nodePrev.nodeEnd1, 0.0f, d->prevWazaHitscanNodeEnd1);
                glm_mat4_mulv3(rotation, nodePrev.nodeEnd2, 0.0f, d->prevWazaHitscanNodeEnd2);
                glm_vec3_add(d->prevWazaHitscanNodeEnd1, d->position, d->prevWazaHitscanNodeEnd1);
                glm_vec3_add(d->prevWazaHitscanNodeEnd2, d->position, d->prevWazaHitscanNodeEnd2);
            }

            for (uint32_t s = 0; s <= d->currentWaza->numHitscanSamples; s++)
            {
                float_t t = (float_t)s / (float_t)d->currentWaza->numHitscanSamples;
                vec3 pt1, pt2;
                glm_vec3_lerp(nodeEnd1WS, nodeEnd2WS, t, pt1);
                glm_vec3_lerp(d->prevWazaHitscanNodeEnd1, d->prevWazaHitscanNodeEnd2, t, pt2);

                std::vector<std::string> hitGuids;
                if (physengine::lineSegmentCast(pt1, pt2, hitscanLayer, true, hitGuids))
                {
                    float_t attackLvl =
                        (float_t)(d->currentWeaponDurability > 0 ?
                            d->materializedItem->weaponStats.attackPower :
                            d->materializedItem->weaponStats.attackPowerWhenDulled);

                    // Successful hitscan!
                    for (auto& guid : hitGuids)
                    {
                        if (guid == myGuid)
                            continue;

                        DataSerializer ds;
                        ds.dumpString("msg_hitscan_hit");
                        ds.dumpFloat(attackLvl);
                        
                        mat4 rotation;
                        glm_euler_zyx(vec3{ 0.0f, d->facingDirection, 0.0f }, rotation);
                        vec3 facingWazaHSLaunchVelocity;
                        glm_mat4_mulv3(rotation, d->currentWaza->hitscanLaunchVelocity, 0.0f, facingWazaHSLaunchVelocity);
                        ds.dumpVec3(facingWazaHSLaunchVelocity);

                        vec3 setPosition;
                        glm_mat4_mulv3(rotation, d->currentWaza->hitscanLaunchRelPosition, 0.0f, setPosition);
                        glm_vec3_add(d->position, setPosition, setPosition);
                        ds.dumpVec3(setPosition);

                        float_t ignoreYF = (float_t)d->currentWaza->hitscanLaunchRelPositionIgnoreY;
                        ds.dumpFloat(ignoreYF);

                        DataSerialized dsd = ds.getSerializedData();
                        if (em->sendMessage(guid, dsd))
                        {
                            playWazaHitSfx = true;

                            // Take off some durability bc of successful hitscan.
                            if (d->currentWeaponDurability > 0)
                            {
                                d->currentWeaponDurability--;
                                if (d->currentWeaponDurability <= 0)
                                    pushPlayerNotification("Weapon has dulled!", d);
                            }
                        }
                    }
                    // break;  @NOTE: in situations where self gets hit by the hitscan, I don't want the search to end.
                }
            }

            // Update prev hitscan node ends.
            glm_vec3_copy(nodeEnd1WS, d->prevWazaHitscanNodeEnd1);
            glm_vec3_copy(nodeEnd2WS, d->prevWazaHitscanNodeEnd2);

            break;  // @NOTE: there should only be one waza hitscan at a certain time, so since this one got processed, then no need to keep searching for another.  -Timo 2023/08/10
        }
    }

    // Play sound if an attack waza landed.
    if (playWazaHitSfx)
    {
        AudioEngine::getInstance().playSound("res/sfx/wip_EnemyHit_Critical.wav");
        d->wazaHitTimescale = d->wazaHitTimescaleOnHit;
    }

    // Check for entities to suck into vacuum OR force in a force zone.
    bool forceZoneEnabled = (d->currentWaza->forceZone.enabled && d->wazaTimer >= d->currentWaza->forceZone.timeFrom && d->wazaTimer <= d->currentWaza->forceZone.timeTo);
    if (d->currentWaza->vacuumSuckIn.enabled || forceZoneEnabled)
    {
        mat4 rotation;
        glm_euler_zyx(vec3{ 0.0f, d->facingDirection, 0.0f }, rotation);
        vec3 suckPositionWS, forceZoneOriginWS;
        if (d->currentWaza->vacuumSuckIn.enabled)
        {
            glm_mat4_mulv3(rotation, d->currentWaza->vacuumSuckIn.position, 0.0f, suckPositionWS);
            glm_vec3_add(suckPositionWS, d->position, suckPositionWS);
        }
        if (forceZoneEnabled)
        {
            glm_mat4_mulv3(rotation, d->currentWaza->forceZone.origin, 0.0f, forceZoneOriginWS);
            glm_vec3_add(forceZoneOriginWS, d->position, forceZoneOriginWS);
        }

        // @COPYPASTA
        for (size_t i = 0; i < physengine::getNumCapsules(); i++)
        {
            physengine::CapsulePhysicsData* otherCPD = physengine::getCapsuleByIndex(i);
            if (otherCPD->entityGuid == myGuid)
                continue;  // Don't vacuum/force self!

            // Vacuum suck in.
            if (d->currentWaza->vacuumSuckIn.enabled)
            {
                vec3 deltaPosition;
                glm_vec3_sub(suckPositionWS, otherCPD->basePosition, deltaPosition);
                float_t radius = d->currentWaza->vacuumSuckIn.radius;
                if (glm_vec3_norm2(deltaPosition) < radius * radius)
                {
                    DataSerializer ds;
                    ds.dumpString("msg_vacuum_suck_in");
                    ds.dumpVec3(suckPositionWS);
                    ds.dumpVec3(deltaPosition);
                    ds.dumpFloat(d->currentWaza->vacuumSuckIn.radius);  // Unneeded maybe.
                    ds.dumpFloat(d->currentWaza->vacuumSuckIn.strength);

                    DataSerialized dsd = ds.getSerializedData();
                    em->sendMessage(otherCPD->entityGuid, dsd);
                }

                // @DEBUG: visualization that shows how far away vacuum radius is.
                vec3 midpt;
                float_t t = radius / glm_vec3_norm(deltaPosition);
                glm_vec3_lerp(suckPositionWS, otherCPD->basePosition, t, midpt);
                if (glm_vec3_norm2(deltaPosition) < radius * radius)
                {
                    physengine::drawDebugVisLine(suckPositionWS, otherCPD->basePosition, physengine::DebugVisLineType::SUCCESS);
                    physengine::drawDebugVisLine(otherCPD->basePosition, midpt, physengine::DebugVisLineType::KIKKOARMY);
                }
                else
                {
                    physengine::drawDebugVisLine(suckPositionWS, midpt, physengine::DebugVisLineType::AUDACITY);
                    physengine::drawDebugVisLine(midpt, otherCPD->basePosition, physengine::DebugVisLineType::VELOCITY);
                }
            }

            // Force zone.
            if (forceZoneEnabled)
            {
                vec3 deltaPosition;
                glm_vec3_sub(forceZoneOriginWS, otherCPD->basePosition, deltaPosition);
                vec3 deltaPositionAbs;
                glm_vec3_abs(deltaPosition, deltaPositionAbs);
                vec3 boundsComparison;
                glm_vec3_sub(d->currentWaza->forceZone.bounds, deltaPositionAbs, boundsComparison);
                if (deltaPositionAbs[0] < d->currentWaza->forceZone.bounds[0] &&
                    deltaPositionAbs[1] < d->currentWaza->forceZone.bounds[1] &&
                    deltaPositionAbs[2] < d->currentWaza->forceZone.bounds[2])
                {
                    // Within force zone.
                    DataSerializer ds;
                    ds.dumpString("msg_apply_force_zone");
                    ds.dumpVec3(d->currentWaza->forceZone.forceVelocity);

                    DataSerialized dsd = ds.getSerializedData();
                    em->sendMessage(otherCPD->entityGuid, dsd);
                }
            }
        }
    }

    // End waza if duration has passed. (ignore if duration is set to negative number; infinite time).
    d->wazaTimer++;
    if (d->currentWaza->duration >= 0 &&
        d->wazaTimer > d->currentWaza->duration)
    {
        inoutNextWaza.nextWaza = d->currentWaza->onDurationPassedWazaPtr;
        inoutNextWaza.set = true;
    }
}

void setWazaToCurrent(Character_XData* d, Character_XData::AttackWaza* nextWaza)
{
    d->currentWaza = nextWaza;
    d->wazaVelocityDecay = 0.0f;
    glm_vec3_copy((d->currentWaza != nullptr && d->currentWaza->velocitySettings.size() > 0 && d->currentWaza->velocitySettings[0].executeAtTime == 0) ? d->currentWaza->velocitySettings[0].velocity : vec3{ 0.0f, 0.0f, 0.0f }, d->wazaVelocity);  // @NOTE: this doesn't work if the executeAtTime's aren't sorted asc.
    d->wazaTimer = 0;
    if (d->currentWaza == nullptr)
        d->characterRenderObj->animator->setState("StateIdle");  // @TODO: this is a crutch.... need to turn this into more of a trigger based system.
    else
        d->characterRenderObj->animator->setState(d->currentWaza->animationState);
    d->characterRenderObj->animator->setMask("MaskCombatMode", (d->currentWaza == nullptr));
}

Character::Character(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds) : Entity(em, ds), _data(new Character_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;
    _data->camera = camera;

    if (ds)
        load(*ds);

    _data->staminaData.currentStamina = (float_t)_data->staminaData.maxStamina;

    _data->weaponAttachmentJointName = "Back Attachment";
    std::vector<vkglTF::Animator::AnimatorCallback> animatorCallbacks = {
        {
            "EventEnableMCM", [&]() {
                _data->characterRenderObj->animator->setMask("MaskCombatMode", true);
            }
        },
        {
            "EventDisableMCM", [&]() {
                _data->characterRenderObj->animator->setMask("MaskCombatMode", false);
            }
        },
        {
            "EventSetAttachmentToHand", [&]() {
                std::cout << "TO HAND" << std::endl;
                _data->weaponAttachmentJointName = "Hand Attachment";
            }
        },
        {
            "EventSetAttachmentToBack", [&]() {
                std::cout << "TO BACK" << std::endl;
                _data->weaponAttachmentJointName = "Back Attachment";
            }
        },
        {
            "EventMaterializeBlade", [&]() {
                std::cout << "MATERIALIZE BLADE" << std::endl;
                _data->weaponRenderObj->renderLayer = RenderLayer::VISIBLE;  // @TODO: in the future will have model switching.
                // AudioEngine::getInstance().playSound("res/sfx/wip_Pl_Kago_Ready.wav");
                AudioEngine::getInstance().playSound("res/sfx/wip_Weapon_Lsword_035_Blur01.wav");
            }
        },
        {
            "EventHokasuBlade", [&]() {
                std::cout << "HOKASU BLADE" << std::endl;
                _data->weaponRenderObj->renderLayer = RenderLayer::INVISIBLE;  // @TODO: in the future will have model switching.
                // @TODO: leave the item on the ground if you wanna reattach or use or litter.
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_Pl_IceBreaking00.wav",
                    "res/sfx/wip_Pl_IceBreaking01.wav",
                    "res/sfx/wip_Pl_IceBreaking02.wav",
                });
            }
        },
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
            "EventPlaySFXGustWall", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_hollow_knight_sfx/hero_nail_art_great_slash.wav",
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
    vkglTF::Model* handleModel = _data->rom->getModel("Handle", this, [](){});
    vkglTF::Model* weaponModel = _data->rom->getModel("WingWeapon", this, [](){});
    _data->rom->registerRenderObjects({
            {
                .model = characterModel,
                .animator = new vkglTF::Animator(characterModel, animatorCallbacks),
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = getGUID(),
            },
            {
                .model = handleModel,
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = getGUID(),
            },
            {
                .model = weaponModel,
                .renderLayer = RenderLayer::INVISIBLE,
                .attachedEntityGuid = getGUID(),
            },
        },
        { &_data->characterRenderObj, &_data->handleRenderObj, &_data->weaponRenderObj }
    );

    // @HARDCODED: there should be a sensing algorithm to know which lightgrid to assign itself to.
    for (auto& inst : _data->characterRenderObj->calculatedModelInstances)
        inst.voxelFieldLightingGridID = 1;
    for (auto& inst : _data->handleRenderObj->calculatedModelInstances)
        inst.voxelFieldLightingGridID = 1;
    for (auto& inst : _data->weaponRenderObj->calculatedModelInstances)
        inst.voxelFieldLightingGridID = 1;

    _data->cpd = physengine::createCapsule(getGUID(), 0.5f, 1.0f);  // Total height is 2, but r*2 is subtracted to get the capsule height (i.e. the line segment length that the capsule rides along)
    glm_vec3_copy(_data->position, _data->cpd->basePosition);
    glm_vec3_copy(_data->cpd->basePosition, _data->prevCPDBasePosition);

    if (_data->characterType == CHARACTER_TYPE_PLAYER)
    {
        _data->camera->mainCamMode.setMainCamTargetObject(_data->characterRenderObj);  // @NOTE: I believe that there should be some kind of main camera system that targets the player by default but when entering different volumes etc. the target changes depending.... essentially the system needs to be more built out imo

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

        auto loadWazasLambda = [&]() {
            _data->wazaSet.clear();
            initWazaSetFromFile(_data->wazaSet, "res/waza/default_waza.hwac");
            initWazaSetFromFile(_data->wazaSet, "res/waza/air_waza.hwac");
        };
        hotswapres::addReloadCallback("res/waza/default_waza.hwac", this, loadWazasLambda);
        hotswapres::addReloadCallback("res/waza/air_waza.hwac", this, loadWazasLambda);
        loadWazasLambda();
    }
}

Character::~Character()
{
    hotswapres::removeOwnedCallbacks(this);

    if (_data->notification.message != nullptr)
        textmesh::destroyAndUnregisterTextMesh(_data->notification.message);
    textmesh::destroyAndUnregisterTextMesh(_data->uiMaterializeItem);

    if (globalState::playerGUID == getGUID() ||
        globalState::playerPositionRef == &_data->cpd->basePosition)
    {
        globalState::playerGUID = "";
        globalState::playerPositionRef = nullptr;
    }

    delete _data->characterRenderObj->animator;
    _data->rom->unregisterRenderObjects({ _data->characterRenderObj, _data->handleRenderObj, _data->weaponRenderObj });
    _data->rom->removeModelCallbacks(this);

    physengine::destroyCapsule(_data->cpd);

    delete _data;
}

void updateWazaTimescale(const float_t& physicsDeltaTime, Character_XData* d)
{
    d->wazaHitTimescale = physutil::lerp(d->wazaHitTimescale, 1.0f, physicsDeltaTime * d->wazaHitTimescale * d->wazaHitTimescaleReturnToOneSpeed);
    if (d->wazaHitTimescale > 0.999f)
        d->wazaHitTimescale = 1.0f;
    globalState::timescale = d->wazaHitTimescale;
}

void defaultPhysicsUpdate(const float_t& physicsDeltaTime, Character_XData* d, EntityManager* em, const std::string& myGuid)
{
    if (d->currentWaza == nullptr)
    {
        //
        // Calculate input
        //
        vec2 input = GLM_VEC2_ZERO_INIT;

        if (d->characterType == CHARACTER_TYPE_PLAYER)
        {
            input[0] += input::keyLeftPressed  ? -1.0f : 0.0f;
            input[0] += input::keyRightPressed ?  1.0f : 0.0f;
            input[1] += input::keyUpPressed    ?  1.0f : 0.0f;
            input[1] += input::keyDownPressed  ? -1.0f : 0.0f;
        }

        if (d->disableInput || d->knockbackMode > Character_XData::KnockbackStage::NONE)
            input[0] = input[1] = 0.0f;

        vec3 flatCameraFacingDirection = {
            d->camera->sceneCamera.facingDirection[0],
            0.0f,
            d->camera->sceneCamera.facingDirection[2]
        };
        glm_normalize(flatCameraFacingDirection);

        glm_vec3_scale(flatCameraFacingDirection, input[1], d->worldSpaceInput);
        vec3 up = { 0.0f, 1.0f, 0.0f };
        vec3 flatCamRight;
        glm_vec3_cross(flatCameraFacingDirection, up, flatCamRight);
        glm_normalize(flatCamRight);
        glm_vec3_muladds(flatCamRight, input[0], d->worldSpaceInput);

        bool isMoving = glm_vec3_norm2(d->worldSpaceInput) < 0.01f;
        if (isMoving)
        {
            glm_vec3_zero(d->worldSpaceInput);
            if (d->prevIsGrounded &&
                (d->prevIsGrounded != d->prevPrevIsGrounded ||
                isMoving != d->prevIsMoving))
                d->characterRenderObj->animator->setTrigger("goto_idle");
        }
        else
        {
            float_t magnitude = glm_clamp_zo(glm_vec3_norm(d->worldSpaceInput));
            glm_vec3_scale_as(d->worldSpaceInput, magnitude, d->worldSpaceInput);
            if (d->prevIsGrounded)
                d->facingDirection = atan2f(d->worldSpaceInput[0], d->worldSpaceInput[2]);
            if (d->prevIsGrounded &&
                (d->prevIsGrounded != d->prevPrevIsGrounded ||
                isMoving != d->prevIsMoving))
                d->characterRenderObj->animator->setTrigger("goto_run");
        }
        if (!d->prevIsGrounded &&
            d->prevIsGrounded != d->prevPrevIsGrounded &&
            !d->prevPerformedJump)
            d->characterRenderObj->animator->setTrigger("goto_fall");
        d->prevIsMoving = isMoving;
        d->prevPrevIsGrounded = d->prevIsGrounded;
    }
    else
    {
        glm_vec3_zero(d->worldSpaceInput);  // Filter movement until the waza is finished.
        d->inputFlagRelease = false;  // @NOTE: @TODO: Idk if this is appropriate or wanted behavior.
    }

    //
    // Process weapon attack input
    //
    bool wazaInputFocus = false;
    if (d->materializedItem != nullptr &&
        d->materializedItem->type == globalState::WEAPON)
    {
        Character_XData::AttackWaza::WazaInput outWazaInputs[MAX_SIMULTANEOUS_WAZA_INPUTS];
        size_t outNumWazaInputs = 0;
        if (!d->disableInput && d->characterType == CHARACTER_TYPE_PLAYER)
        {
            wazaInputFocus = true;
            processInputForWaza(d, outWazaInputs, outNumWazaInputs);
        }

        NextWazaPtr nextWaza;
        if (outNumWazaInputs > 0)
            processWazaInput(d, outWazaInputs, outNumWazaInputs, nextWaza);
        if (d->currentWaza != nullptr)
            processWazaUpdate(d, em, physicsDeltaTime, myGuid, nextWaza);
        if (nextWaza.set)
            setWazaToCurrent(d, nextWaza.nextWaza);
    }
    if (wazaInputFocus)
    {
        d->inputFlagJump = false;
        d->inputFlagAttack = false;
    }

    //
    // Process input flags
    //
    if (d->inputFlagAttack)
    {
        processAttack(d);
        d->inputFlagAttack = false;
    }

    if (d->inputFlagRelease)
    {
        processRelease(d);
        d->inputFlagRelease = false;
    }

    //
    // Update stamina gauge
    //
    if (d->staminaData.refillTimer > 0.0f)
        d->staminaData.refillTimer -= physicsDeltaTime;
    else if (d->staminaData.currentStamina < d->staminaData.maxStamina)
    {
        d->staminaData.depletionOverflow = 0.0f;
        changeStamina(d, d->staminaData.refillRate * physicsDeltaTime, false);
    }

    if (d->characterType == CHARACTER_TYPE_PLAYER)
    {
        if (d->staminaData.changedTimer > 0.0f)
        {
            d->uiStamina->excludeFromBulkRender = false;
            d->staminaData.changedTimer -= physicsDeltaTime;
        }
        else
            d->uiStamina->excludeFromBulkRender = true;
    }

    //
    // Update movement and collision
    //
    constexpr float_t gravity = -0.98f / 0.025f;  // @TODO: put physicsengine constexpr of `physicsDeltaTime` into the header file and rename it to `constantPhysicsDeltaTime` and replace the 0.025f with it.
    constexpr float_t jumpHeight = 2.0f;
    d->gravityForce += gravity * (d->currentWaza != nullptr ? d->currentWaza->gravityMultiplier : 1.0f) * physicsDeltaTime;
    d->prevPerformedJump = false;  // For animation state machine (differentiate goto_jump and goto_fall)
    if (d->prevIsGrounded && d->inputFlagJump)
    {
        d->gravityForce = std::sqrtf(jumpHeight * 2.0f * std::abs(gravity));  // @COPYPASTA
        d->prevIsGrounded = false;
        d->inputFlagJump = false;
        d->prevPerformedJump = true;
        d->characterRenderObj->animator->setTrigger("goto_jump");
    }

    vec3 velocity = GLM_VEC3_ZERO_INIT;
    if (d->currentWaza == nullptr)
    {
        if (d->prevIsGrounded && d->knockbackMode == Character_XData::KnockbackStage::NONE)
            glm_vec3_scale(d->worldSpaceInput, d->inputMaxXZSpeed * physicsDeltaTime, velocity);
        else
        {
            vec3 targetVelocity;
            glm_vec3_scale(d->worldSpaceInput, d->inputMaxXZSpeed * physicsDeltaTime, targetVelocity);

            vec3 flatDeltaPosition;
            glm_vec3_sub(d->cpd->basePosition, d->prevCPDBasePosition, flatDeltaPosition);
            flatDeltaPosition[1] = 0.0f;

            vec3 targetDelta;
            glm_vec3_sub(targetVelocity, flatDeltaPosition, targetDelta);
            if (glm_vec3_norm2(targetDelta) > 0.000001f)
            {
                vec3 flatDeltaPositionNormalized;
                glm_vec3_normalize_to(flatDeltaPosition, flatDeltaPositionNormalized);
                vec3 targetVelocityNormalized;
                glm_vec3_normalize_to(targetVelocity, targetVelocityNormalized);
                bool useAcceleration = (glm_vec3_dot(targetVelocityNormalized, flatDeltaPositionNormalized) < 0.0f || glm_vec3_norm2(targetVelocity) > glm_vec3_norm2(flatDeltaPosition));
                float_t maxAllowedDeltaMagnitude = (useAcceleration ? d->midairXZAcceleration : d->midairXZDeceleration) * physicsDeltaTime;

                // @NOTE: Assumption is that during recovery and knocked back stages, the input is set to 0,0
                //        thus deceleration is the acceleration method at all times.
                if (d->prevIsGrounded)
                    if (d->knockbackMode == Character_XData::KnockbackStage::RECOVERY)
                    {
                        maxAllowedDeltaMagnitude = d->recoveryGroundedXZDeceleration * physicsDeltaTime;
                    }
                    else if (d->knockbackMode == Character_XData::KnockbackStage::KNOCKED_UP)
                    {
                        maxAllowedDeltaMagnitude = d->knockedbackGroundedXZDeceleration * physicsDeltaTime;
                    }

                if (glm_vec3_norm2(targetDelta) > maxAllowedDeltaMagnitude * maxAllowedDeltaMagnitude)
                    glm_vec3_scale_as(targetDelta, maxAllowedDeltaMagnitude, targetDelta);

                glm_vec3_add(flatDeltaPosition, targetDelta, velocity);
            }
            else
            {
                glm_vec3_copy(flatDeltaPosition, velocity);
            }

            // Process knockback stages. @TODO: put this into its own function/process.
            if (d->knockbackMode == Character_XData::KnockbackStage::KNOCKED_UP)
            {
                if (d->knockedbackTimer < 0.0f)
                    d->knockbackMode = Character_XData::KnockbackStage::RECOVERY;
                else
                    d->knockedbackTimer -= physicsDeltaTime;
            }
            if (d->knockbackMode == Character_XData::KnockbackStage::RECOVERY &&
                d->prevIsGrounded &&
                std::abs(velocity[0]) < 0.001f &&
                std::abs(velocity[2]) < 0.001f)
                d->knockbackMode = Character_XData::KnockbackStage::NONE;
        }
    }
    else
    {
        // Hold in midair if wanted by waza
        if (d->currentWaza->holdMidair &&
            d->currentWaza->holdMidairTimeFrom < 0 ||
            (d->currentWaza->holdMidairTimeFrom <= d->wazaTimer - 1 &&
            d->currentWaza->holdMidairTimeTo >= d->wazaTimer - 1))
        {
            d->gravityForce = std::max(0.0f, d->gravityForce);
        }

        // Add waza velocity
        if (glm_vec3_norm2(d->wazaVelocity) > 0.0f)
        {
            mat4 rotation;
            glm_euler_zyx(vec3{ 0.0f, d->facingDirection, 0.0f }, rotation);
            vec3 facingWazaVelocity;
            glm_mat4_mulv3(rotation, d->wazaVelocity, 0.0f, facingWazaVelocity);
            glm_vec3_scale(facingWazaVelocity, physicsDeltaTime, velocity);
            
            // Execute jump.
            if (d->wazaVelocity[1] > 0.0f)  // @CHECK: I think that maybe... negative velocities should be copied to `gravityForce` as well. CHECK!  -Timo 2023/08/14
            {
                d->gravityForce = d->wazaVelocity[1];
                d->prevIsGrounded = false;

                d->wazaVelocity[1] = 0.0f;  // @REPLY: maybe this line is what the >0 check is for with the vertical waza velocity????  -Timo 2023/08/14
                velocity[1] = 0.0f;  // @AMEND: I added this line after analyzing this code block... bc velocity[1] gets a += later with gravityforce leading it, I think that wazaVelocity[1] shouldn't be added on twice, so I added this line. It's sure to require some adjusting of the hwacs.  -Timo 2023/08/14
            }
        }
    }

    if (d->triggerLaunchVelocity)
    {
        if (d->launchRelPosIgnoreY)
        {
            d->cpd->basePosition[0] = d->launchSetPosition[0];
            d->cpd->basePosition[2] = d->launchSetPosition[2];
        }
        else
            glm_vec3_copy(d->launchSetPosition, d->cpd->basePosition);
        velocity[0] = d->launchVelocity[0] * physicsDeltaTime;
        velocity[2] = d->launchVelocity[2] * physicsDeltaTime;
        d->gravityForce = d->launchVelocity[1];
        if (d->gravityForce > 0.0f)
            d->prevIsGrounded = false;
        d->iframesTimer = d->iframesTime;
        d->knockbackMode = Character_XData::KnockbackStage::KNOCKED_UP;
        d->knockedbackTimer = d->knockedbackTime;
        setWazaToCurrent(d, nullptr);

        d->triggerLaunchVelocity = false;
    }

    if (d->triggerApplyForceZone)
    {
        velocity[0] = d->forceZoneVelocity[0] * physicsDeltaTime;  // @COPYPASTA
        velocity[2] = d->forceZoneVelocity[2] * physicsDeltaTime;
        d->gravityForce = d->forceZoneVelocity[1];
        if (d->gravityForce > 0.0f)
            d->prevIsGrounded = false;
        setWazaToCurrent(d, nullptr);

        if (d->forceZoneVelocity[1] < 0.0f && d->prevIsGrounded)
        {
            d->characterRenderObj->animator->setTrigger("goto_getting_pressed");
            d->inGettingPressedAnim = true;
        }

        d->triggerApplyForceZone = false;
    }
    else
    {
        if (d->inGettingPressedAnim)  // Exit pressed animation
        {
            d->characterRenderObj->animator->setTrigger("goto_get_out_getting_pressed");
            d->inGettingPressedAnim = false;
        }
    }

    if (d->prevIsGrounded && d->prevGroundNormal[1] < 0.999f)
    {
        versor groundNormalRotation;
        glm_quat_from_vecs(vec3{ 0.0f, 1.0f, 0.0f }, d->prevGroundNormal, groundNormalRotation);
        mat3 groundNormalRotationM3;
        glm_quat_mat3(groundNormalRotation, groundNormalRotationM3);
        glm_mat3_mulv(groundNormalRotationM3, velocity, velocity);
    }

    velocity[1] += d->gravityForce * physicsDeltaTime;

    if (d->triggerSuckIn)
    {
        glm_vec3_scale(d->suckInVelocity, physicsDeltaTime, velocity);  // Completely overwrite velocity.

        vec3 deltaPosition;  // Check if going to move past target position. If so, cut the velocity short.
        glm_vec3_sub(d->suckInTargetPosition, d->cpd->basePosition, deltaPosition);
        if (glm_vec3_norm2(deltaPosition) < glm_vec3_norm2(velocity))
            glm_vec3_copy(deltaPosition, velocity);

        d->gravityForce = velocity[1];
        d->triggerSuckIn = false;
    }

    glm_vec3_copy(d->cpd->basePosition, d->prevCPDBasePosition);
    physengine::moveCapsuleAccountingForCollision(*d->cpd, velocity, d->prevIsGrounded, d->prevGroundNormal);
    glm_vec3_copy(d->cpd->basePosition, d->position);

    d->prevIsGrounded = (d->prevGroundNormal[1] >= 0.707106781187);  // >=45 degrees
    if (d->prevIsGrounded)
        d->gravityForce = 0.0f;
}

void calculateBladeStartEndFromHandAttachment(Character_XData* d, vec3& bladeStart, vec3& bladeEnd)
{
    mat4 attachmentJointMat;
    d->characterRenderObj->animator->getJointMatrix(d->attackWazaEditor.bladeBoneName, attachmentJointMat);
    glm_mat4_mulv3(attachmentJointMat, vec3{ 0.0f, d->attackWazaEditor.bladeDistanceStartEnd[0], 0.0f }, 1.0f, bladeStart);
    glm_mat4_mulv3(attachmentJointMat, vec3{ 0.0f, d->attackWazaEditor.bladeDistanceStartEnd[1], 0.0f }, 1.0f, bladeEnd);
}

void attackWazaEditorPhysicsUpdate(const float_t& physicsDeltaTime, Character_XData* d)
{
    if (d->attackWazaEditor.triggerRecalcWazaCache)
    {
        Character_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

        d->attackWazaEditor.minTick = 0;
        d->attackWazaEditor.maxTick = aw.duration >= 0 ? aw.duration : 100;  // @HARDCODE: if duration is infinite, just cap it at 100.  -Timo 2023/09/22

        d->characterRenderObj->animator->setState(aw.animationState, d->attackWazaEditor.currentTick * physicsDeltaTime);

        d->attackWazaEditor.triggerRecalcWazaCache = false;
    }

    if (d->attackWazaEditor.triggerRecalcHitscanLaunchVelocityCache)
    {
        Character_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

        d->attackWazaEditor.hitscanLaunchVelocitySimCache.clear();
        vec3s currentPosition;
        glm_vec3_copy(aw.hitscanLaunchRelPosition, currentPosition.raw);
        vec3  launchVelocityCopy;
        glm_vec3_copy(aw.hitscanLaunchVelocity, launchVelocityCopy);

        float_t knockedbackTimer = d->knockedbackTime;
        Character_XData::KnockbackStage knockbackMode = Character_XData::KnockbackStage::KNOCKED_UP;

        for (size_t i = 0; i < 100; i++)
        {
            vec3 deltaPosition;
            glm_vec3_scale(launchVelocityCopy, physicsDeltaTime, deltaPosition);
            glm_vec3_add(currentPosition.raw, deltaPosition, currentPosition.raw);
            currentPosition.y = std::max(0.0f, currentPosition.y);
            d->attackWazaEditor.hitscanLaunchVelocitySimCache.push_back(currentPosition);

            launchVelocityCopy[1] -= 0.98f;  // @HARDCODE: Should match `constexpr float_t gravity`

            vec3 xzVelocityDampen = {
                -launchVelocityCopy[0],
                0.0f,
                -launchVelocityCopy[2],
            };
            
            float_t maxAllowedDeltaMagnitude = d->midairXZDeceleration;
            bool prevIsGrounded = (currentPosition.y <= 0.0f);
            if (prevIsGrounded)  // @COPYPASTA
                    if (knockbackMode == Character_XData::KnockbackStage::RECOVERY)
                    {
                        maxAllowedDeltaMagnitude = d->recoveryGroundedXZDeceleration;
                    }
                    else if (knockbackMode == Character_XData::KnockbackStage::KNOCKED_UP)
                    {
                        maxAllowedDeltaMagnitude = d->knockedbackGroundedXZDeceleration;
                    }

            if (glm_vec3_norm2(xzVelocityDampen) > maxAllowedDeltaMagnitude * maxAllowedDeltaMagnitude)
                glm_vec3_scale_as(xzVelocityDampen, maxAllowedDeltaMagnitude, xzVelocityDampen);
            glm_vec3_add(launchVelocityCopy, xzVelocityDampen, launchVelocityCopy);

            // @COPYPASTA
            if (knockbackMode == Character_XData::KnockbackStage::KNOCKED_UP)
            {
                if (knockedbackTimer < 0.0f)
                {
                    knockbackMode = Character_XData::KnockbackStage::RECOVERY;
                }
                else
                    knockedbackTimer -= physicsDeltaTime;
            }
        }

        d->attackWazaEditor.hitscanLaunchAndSelfVelocityAwaseIndex = 0;
        d->attackWazaEditor.triggerRecalcHitscanLaunchVelocityCache = false;
    }

    if (d->attackWazaEditor.triggerRecalcSelfVelocitySimCache)
    {
        Character_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

        d->attackWazaEditor.selfVelocitySimCache.clear();
        vec3s   currentPosition = GLM_VEC3_ZERO_INIT;
        vec3    currentVelocity = GLM_VEC3_ZERO_INIT;
        float_t currentVelocityDecay = 0.0f;
        for (size_t i = 0; i < 100; i++)
        {
            for (auto& velocitySetting : aw.velocitySettings)
                if (velocitySetting.executeAtTime == i)
                {
                    glm_vec3_copy(velocitySetting.velocity, currentVelocity);
                    break;
                }

            vec3 deltaPosition;
            glm_vec3_scale(currentVelocity, physicsDeltaTime, deltaPosition);
            glm_vec3_add(currentPosition.raw, deltaPosition, currentPosition.raw);
            currentPosition.y = std::max(0.0f, currentPosition.y);
            d->attackWazaEditor.selfVelocitySimCache.push_back(currentPosition);

            for (auto& velocityDecaySetting : aw.velocityDecaySettings)
                if (velocityDecaySetting.executeAtTime == i)
                {
                    currentVelocityDecay = velocityDecaySetting.velocityDecay;
                    break;
                }

            if (currentVelocityDecay != 0.0f)
            {
                vec3 flatCurrentVelocity = {
                    currentVelocity[0],
                    0.0f,
                    currentVelocity[2],
                };
                float_t newNorm = std::max(0.0f, glm_vec3_norm(flatCurrentVelocity) - currentVelocityDecay);
                glm_vec3_scale_as(flatCurrentVelocity, newNorm, flatCurrentVelocity);
                currentVelocity[0] = flatCurrentVelocity[0];
                currentVelocity[2] = flatCurrentVelocity[2];
            }

            currentVelocity[1] -= 0.98f;  // @HARDCODE: Should match `constexpr float_t gravity`
        }

        d->attackWazaEditor.hitscanLaunchAndSelfVelocityAwaseIndex = 0;
        d->attackWazaEditor.triggerRecalcSelfVelocitySimCache = false;
    }

    if (d->attackWazaEditor.triggerBakeHitscans)
    {
        Character_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

        // Fill in hitscan flow nodes according to baked range.
        aw.hitscanNodes.clear();
        for (int16_t i = d->attackWazaEditor.bakeHitscanStartTick; i <= d->attackWazaEditor.bakeHitscanEndTick; i++)
        {
            d->characterRenderObj->animator->setState(aw.animationState, i * physicsDeltaTime, true);

            Character_XData::AttackWaza::HitscanFlowNode hfn;
            calculateBladeStartEndFromHandAttachment(d, hfn.nodeEnd1, hfn.nodeEnd2);
            glm_vec3_scale(hfn.nodeEnd1, d->modelSize, hfn.nodeEnd1);
            glm_vec3_scale(hfn.nodeEnd2, d->modelSize, hfn.nodeEnd2);
            hfn.executeAtTime = i;
            aw.hitscanNodes.push_back(hfn);
        }

        // Fill out the export string.
        d->attackWazaEditor.hitscanSetExportString = "";
        for (size_t i = 0; i < aw.hitscanNodes.size(); i++)
        {
            d->attackWazaEditor.hitscanSetExportString +=
                "hitscan            " +
                std::to_string(aw.hitscanNodes[i].nodeEnd1[0]) + "," +
                std::to_string(aw.hitscanNodes[i].nodeEnd1[1]) + "," +
                std::to_string(aw.hitscanNodes[i].nodeEnd1[2]) + "    " +
                std::to_string(aw.hitscanNodes[i].nodeEnd2[0]) + "," +
                std::to_string(aw.hitscanNodes[i].nodeEnd2[1]) + "," +
                std::to_string(aw.hitscanNodes[i].nodeEnd2[2]);
            if (i > 0)
                d->attackWazaEditor.hitscanSetExportString +=
                    "    " + std::to_string(aw.hitscanNodes[i].executeAtTime);
            d->attackWazaEditor.hitscanSetExportString += "\n";
        }

        d->attackWazaEditor.triggerBakeHitscans = false;
    }

    // Draw flow node lines
    std::vector<Character_XData::AttackWaza::HitscanFlowNode>& hnodes = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanNodes;
    for (size_t i = 1; i < hnodes.size(); i++)
    {
        vec3 nodeEnd1_i, nodeEnd1_i1, nodeEnd2_i, nodeEnd2_i1;
        glm_vec3_add(hnodes[i].nodeEnd1, d->position, nodeEnd1_i);
        glm_vec3_add(hnodes[i - 1].nodeEnd1, d->position, nodeEnd1_i1);
        glm_vec3_add(hnodes[i].nodeEnd2, d->position, nodeEnd2_i);
        glm_vec3_add(hnodes[i - 1].nodeEnd2, d->position, nodeEnd2_i1);
        physengine::drawDebugVisLine(nodeEnd1_i1, nodeEnd1_i, physengine::DebugVisLineType::KIKKOARMY);
        physengine::drawDebugVisLine(nodeEnd2_i1, nodeEnd2_i, physengine::DebugVisLineType::KIKKOARMY);

        vec3 nodeEndMid_i, nodeEndMid_i1;
        glm_vec3_lerp(nodeEnd1_i1, nodeEnd2_i1, 0.5f, nodeEndMid_i1);
        glm_vec3_lerp(nodeEnd1_i, nodeEnd2_i, 0.5f, nodeEndMid_i);
        physengine::drawDebugVisLine(nodeEndMid_i1, nodeEndMid_i, physengine::DebugVisLineType::KIKKOARMY);
    }

    // Draw hitscan launch velocity vis line.
    std::vector<vec3s>& hslvsc = d->attackWazaEditor.hitscanLaunchVelocitySimCache;
    for (size_t i = 1; i < hslvsc.size(); i++)
    {
        vec3 hsLaunchVeloPositionWS_i, hsLaunchVeloPositionWS_i1;
        glm_vec3_add(d->position, hslvsc[i].raw, hsLaunchVeloPositionWS_i);
        glm_vec3_add(d->position, hslvsc[i - 1].raw, hsLaunchVeloPositionWS_i1);
        physengine::drawDebugVisLine(hsLaunchVeloPositionWS_i1, hsLaunchVeloPositionWS_i, (d->attackWazaEditor.hitscanLaunchAndSelfVelocityAwaseIndex == (int32_t)i ? physengine::DebugVisLineType::SUCCESS : physengine::DebugVisLineType::VELOCITY));
    }

    // Draw self launch velocity vis line.
    std::vector<vec3s>& svsc = d->attackWazaEditor.selfVelocitySimCache;
    for (size_t i = 1; i < svsc.size(); i++)
    {
        vec3 selfVeloPositionWS_i, selfVeloPositionWS_i1;
        glm_vec3_add(d->position, svsc[i].raw, selfVeloPositionWS_i);
        glm_vec3_add(d->position, svsc[i - 1].raw, selfVeloPositionWS_i1);
        physengine::drawDebugVisLine(selfVeloPositionWS_i1, selfVeloPositionWS_i, (d->attackWazaEditor.hitscanLaunchAndSelfVelocityAwaseIndex == (int32_t)i ? physengine::DebugVisLineType::SUCCESS : physengine::DebugVisLineType::AUDACITY));
    }

    // Draw suck in lines.
    Character_XData::AttackWaza::VacuumSuckIn& vsi = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].vacuumSuckIn;
    if (vsi.enabled)
    {
        static vec3s line1[] = {
            vec3s{ -1.0f, 0.0f, 0.0f },
            vec3s{  1.0f, 0.0f, 0.0f },
        };
        static vec3s line2[] = {
            vec3s{ 0.0f, -1.0f, 0.0f },
            vec3s{ 0.0f,  1.0f, 0.0f },
        };
        static vec3s line3[] = {
            vec3s{ 0.0f, 0.0f, -1.0f },
            vec3s{ 0.0f, 0.0f,  1.0f },
        };
        static vec3s* lineList[] = {
            line1, line2, line3,
        };
        for (size_t i = 0; i < 3; i++)
        {
            vec3s* line = lineList[i];
            vec3 pt1, pt2;
            glm_vec3_scale(line[0].raw, vsi.radius, pt1);
            glm_vec3_scale(line[1].raw, vsi.radius, pt2);
            glm_vec3_add(pt1, vsi.position, pt1);
            glm_vec3_add(pt2, vsi.position, pt2);
            glm_vec3_add(pt1, d->position, pt1);
            glm_vec3_add(pt2, d->position, pt2);
            physengine::drawDebugVisLine(pt1, pt2, physengine::DebugVisLineType::SUCCESS);
        }
    }

    // Draw force zone.
    Character_XData::AttackWaza::ForceZone& fz = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].forceZone;
    if (fz.enabled)
    {
        // Force zone aabb
        static vec3s points[] = {
            vec3s{ -1.0f,  1.0f,  1.0f },
            vec3s{  1.0f,  1.0f,  1.0f },
            vec3s{  1.0f,  1.0f, -1.0f },
            vec3s{ -1.0f,  1.0f, -1.0f },
            vec3s{ -1.0f, -1.0f,  1.0f },
            vec3s{  1.0f, -1.0f,  1.0f },
            vec3s{  1.0f, -1.0f, -1.0f },
            vec3s{ -1.0f, -1.0f, -1.0f },
        };
        static size_t indices[] = {
            0, 1,
            1, 2,
            2, 3,
            3, 0,
            4, 5,
            5, 6,
            6, 7,
            7, 4,
            0, 4,
            1, 5,
            2, 6,
            3, 7,
        };
        for (size_t i = 0; i < 12; i++)
        {
            vec3 pt1, pt2;
            glm_vec3_mul(points[indices[i * 2 + 0]].raw, fz.bounds, pt1);
            glm_vec3_mul(points[indices[i * 2 + 1]].raw, fz.bounds, pt2);
            glm_vec3_add(pt1, fz.origin, pt1);
            glm_vec3_add(pt2, fz.origin, pt2);
            glm_vec3_add(pt1, d->position, pt1);
            glm_vec3_add(pt2, d->position, pt2);
            physengine::drawDebugVisLine(pt1, pt2, physengine::DebugVisLineType::VELOCITY);
        }

        // Velocity line
        vec3 veloTo;
        glm_vec3_add(d->position, fz.forceVelocity, veloTo);
        physengine::drawDebugVisLine(d->position, veloTo, physengine::DebugVisLineType::PURPTEAL);
    }

    // Draw visual line showing where weapon hitscan will show up.
    vec3 bladeStart, bladeEnd;
    calculateBladeStartEndFromHandAttachment(d, bladeStart, bladeEnd);
    glm_vec3_scale(bladeStart, d->modelSize, bladeStart);
    glm_vec3_scale(bladeEnd, d->modelSize, bladeEnd);
    glm_vec3_add(bladeStart, d->position, bladeStart);
    glm_vec3_add(bladeEnd, d->position, bladeEnd);
    physengine::drawDebugVisLine(bladeStart, bladeEnd, physengine::DebugVisLineType::YUUJUUFUDAN);
}

void Character::physicsUpdate(const float_t& physicsDeltaTime)
{
    // @DEBUG: for level editor
    _data->disableInput = (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput);
    
    if (_data->wazaHitTimescale < 1.0f)
        updateWazaTimescale(physicsDeltaTime, _data);

    if (_data->characterType == CHARACTER_TYPE_PLAYER)
    {
        // Prevent further processing of update if textbox exists.
        if (textbox::isProcessingMessage())
        {
            _data->uiMaterializeItem->excludeFromBulkRender = true;
            return;
        }
        else
            _data->uiMaterializeItem->excludeFromBulkRender = false;
    }

    // Update invincibility frames timer.
    if (_data->iframesTimer > 0.0f)
        _data->iframesTimer -= physicsDeltaTime;

    // Process physics updates depending on the mode.
    if (_data->attackWazaEditor.isEditingMode)
        attackWazaEditorPhysicsUpdate(physicsDeltaTime, _data);
    else
        defaultPhysicsUpdate(physicsDeltaTime, _data, _em, getGUID());
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

void Character::update(const float_t& deltaTime)
{
    // @DEBUG: for level editor
    _data->disableInput = (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput);

    // Update twitch angle.
    _data->characterRenderObj->animator->setTwitchAngle(_data->attackTwitchAngle);
    _data->attackTwitchAngle = glm_lerp(_data->attackTwitchAngle, 0.0f, std::abs(_data->attackTwitchAngle) * _data->attackTwitchAngleReturnSpeed * 60.0f * deltaTime);

    if (_data->characterType == CHARACTER_TYPE_PLAYER)
    {
        //
        // Handle 'E' action
        //
        if (interactionUIText != nullptr &&
            !interactionGUIDPriorityQueue.empty())
            if (_data->prevIsGrounded && !textbox::isProcessingMessage())
            {
                interactionUIText->excludeFromBulkRender = false;
                if (!_data->disableInput && input::onKeyInteractPress)
                {
                    DataSerializer ds;
                    ds.dumpString("msg_commit_interaction");
                    DataSerialized dsd = ds.getSerializedData();
                    _em->sendMessage(interactionGUIDPriorityQueue.front().guid, dsd);
                }
            }
            else
                interactionUIText->excludeFromBulkRender = true;

        // Notification UI
        if (_data->notification.showMessageTimer > 0.0f)
        {
            _data->notification.showMessageTimer -= deltaTime;
            _data->notification.message->excludeFromBulkRender = (_data->notification.showMessageTimer <= 0.0f);
        }
    }

    if (textbox::isProcessingMessage())
        return;

    if (_data->characterType == CHARACTER_TYPE_PLAYER)
    {
        // Poll keydown inputs.
        if (_data->knockbackMode == Character_XData::KnockbackStage::NONE)
        {
            _data->inputFlagJump |= !_data->disableInput && input::onKeyJumpPress;
            _data->inputFlagAttack |= !_data->disableInput && input::onLMBPress;
            _data->inputFlagRelease |= !_data->disableInput && input::onRMBPress;
        }

        // Change aura
        if (_data->knockbackMode == Character_XData::KnockbackStage::NONE &&
            input::keyAuraPressed)
        {
            if (_data->auraSfxChannelIds.empty())
            {
                // Spin up aura sfx            
                _data->auraSfxChannelIds.push_back(
                    AudioEngine::getInstance().playSound("res/sfx/wip_hollow_knight_sfx/hero_super_dash_burst.wav")  // Aura burst start
                );
                _data->auraSfxChannelIds.push_back(
                    AudioEngine::getInstance().playSound("res/sfx/wip_hollow_knight_sfx/hero_super_dash_loop.wav", true)  // Aura loop
                );
                _data->auraSfxChannelIds.push_back(
                    AudioEngine::getInstance().playSound("res/sfx/wip_hollow_knight_sfx/hero_fury_charm_loop.wav", true)  // Heartbeat loop
                );

                // Search for opponent to target.
                // @COPYPASTA
                physengine::CapsulePhysicsData* closestCPD = nullptr;
                float_t closestDistance = -1.0f;
                for (size_t i = 0; i < physengine::getNumCapsules(); i++)
                {
                    physengine::CapsulePhysicsData* otherCPD = physengine::getCapsuleByIndex(i);
                    if (otherCPD->entityGuid == getGUID())
                        continue;  // Don't target self!

                    float_t thisDistance = glm_vec3_distance2(_data->cpd->basePosition, otherCPD->basePosition);
                    if (closestDistance < 0.0 || thisDistance < closestDistance)
                    {
                        closestCPD = otherCPD;
                        closestDistance = thisDistance;
                    }
                }
                _data->camera->mainCamMode.setOpponentCamTargetObject(closestCPD);
            }
        }
        else
        {
            if (!_data->auraSfxChannelIds.empty())
            {
                // Shut down aura sfx
                for (int32_t id : _data->auraSfxChannelIds)
                    AudioEngine::getInstance().stopChannel(id);
                _data->auraSfxChannelIds.clear();
                
                // Play ending sound
                AudioEngine::getInstance().playSound("res/sfx/wip_hollow_knight_sfx/hero_super_dash_ready.wav");

                // Release targeting opponent.
                _data->camera->mainCamMode.setOpponentCamTargetObject(nullptr);
            }
        }
    }
}

void Character::lateUpdate(const float_t& deltaTime)
{
    if (_data->attackWazaEditor.isEditingMode)
        _data->facingDirection = 0.0f;  // @NOTE: this needs to be facing in the default facing direction so that the hitscan node positions are facing in the default direction when baked.

    //
    // Update position of character and weapon
    //
    vec3 eulerAngles = { 0.0f, _data->facingDirection, 0.0f };
    mat4 rotation;
    glm_euler_zyx(eulerAngles, rotation);

    mat4 transform = GLM_MAT4_IDENTITY_INIT;
    glm_translate(transform, _data->cpd->interpolBasePosition);
    glm_mat4_mul(transform, rotation, transform);
    glm_scale(transform, vec3{ _data->modelSize, _data->modelSize, _data->modelSize });
    glm_mat4_copy(transform, _data->characterRenderObj->transformMatrix);

    mat4 attachmentJointMat;
    _data->characterRenderObj->animator->getJointMatrix(_data->weaponAttachmentJointName, attachmentJointMat);
    glm_mat4_mul(_data->characterRenderObj->transformMatrix, attachmentJointMat, _data->weaponRenderObj->transformMatrix);
    glm_mat4_copy(_data->weaponRenderObj->transformMatrix, _data->handleRenderObj->transformMatrix);
}

void Character::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpString(_data->characterType);
    ds.dumpVec3(_data->position);
    ds.dumpFloat(_data->facingDirection);

    float_t healthF = _data->health;
    ds.dumpFloat(healthF);

    // Harvestable item ids
    float_t numHarvestableItems = (float_t)_data->harvestableItemsIdsToSpawnAfterDeath.size();
    ds.dumpFloat(numHarvestableItems);
    for (size_t id : _data->harvestableItemsIdsToSpawnAfterDeath)
    {
        float_t idF = (float_t)id;
        ds.dumpFloat(idF);
    }

    // Scannable item ids
    float_t numScannableItems = (float_t)_data->scannableItemsIdsToSpawnAfterDeath.size();
    ds.dumpFloat(numScannableItems);
    for (size_t id : _data->scannableItemsIdsToSpawnAfterDeath)
    {
        float_t idF = (float_t)id;
        ds.dumpFloat(idF);
    }
}

void Character::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadString(_data->characterType);
    ds.loadVec3(_data->position);
    ds.loadFloat(_data->facingDirection);

    float_t healthF;
    ds.loadFloat(healthF);
    _data->health = (int32_t)healthF;

    // Harvestable item ids
    float_t numHarvestableItemsF;
    ds.loadFloat(numHarvestableItemsF);
    _data->harvestableItemsIdsToSpawnAfterDeath.resize((size_t)numHarvestableItemsF);
    for (size_t& idRef : _data->harvestableItemsIdsToSpawnAfterDeath)
    {
        float_t idF;
        ds.loadFloat(idF);
        idRef = (size_t)idF;
    }

    // Scannable item ids
    float_t numScannableItemsF;
    ds.loadFloat(numScannableItemsF);
    _data->scannableItemsIdsToSpawnAfterDeath.resize((size_t)numScannableItemsF);
    for (size_t& idRef : _data->scannableItemsIdsToSpawnAfterDeath)
    {
        float_t idF;
        ds.loadFloat(idF);
        idRef = (size_t)idF;
    }
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

bool Character::processMessage(DataSerialized& message)
{
    std::string messageType;
    message.loadString(messageType);

    if (messageType == "msg_request_interaction")
    {
        if (_data->characterType == CHARACTER_TYPE_PLAYER)
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
        }

        return true;
    }
    else if (messageType == "msg_remove_interaction_request")
    {
        if (_data->characterType == CHARACTER_TYPE_PLAYER)
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
        }

        return true;
    }
    else if (messageType == "msg_notify_scannable_item_added" || messageType == "msg_notify_harvestable_item_harvested")
    {
        if (_data->characterType == CHARACTER_TYPE_PLAYER)
        {
            textmesh::regenerateTextMeshMesh(_data->uiMaterializeItem, getUIMaterializeItemText(_data));
        }
        return true;
    }
    else if (messageType == "msg_hitscan_hit")
    {
        // Don't react to hitscan if in invincibility frames.
        if (_data->iframesTimer <= 0.0f)
        {
            float_t attackLvl;
            message.loadFloat(attackLvl);
            _data->health -= (int32_t)attackLvl;

            message.loadVec3(_data->launchVelocity);
            message.loadVec3(_data->launchSetPosition);

            float_t ignoreYF;
            message.loadFloat(ignoreYF);
            _data->launchRelPosIgnoreY = (bool)ignoreYF;

            _data->triggerLaunchVelocity = true;  // @TODO: right here, do calculations for poise and stuff!

            if (_data->health <= 0)
                processOutOfHealth(_em, this, _data);

            return true;
        }
    }
    else if (messageType == "msg_vacuum_suck_in")
    {
        message.loadVec3(_data->suckInTargetPosition);
        vec3 deltaPosition;
        message.loadVec3(deltaPosition);
        float_t radius;
        message.loadFloat(radius);
        float_t strength;
        message.loadFloat(strength);

        float_t deltaPosDist = glm_vec3_norm(deltaPosition);
        float_t oneMinusPropo = 1.0f - (deltaPosDist / radius);  // Strength attenuation saturated to [0-1].
        float_t strengthCooked = strength * oneMinusPropo;
        glm_vec3_scale_as(deltaPosition, strengthCooked * radius, deltaPosition);

        glm_vec3_copy(deltaPosition, _data->suckInVelocity);
        // @DEBUG: vis
        vec3 nxt;
        glm_vec3_add(_data->position, deltaPosition, nxt);
        physengine::drawDebugVisLine(_data->position, nxt);

        _data->triggerSuckIn = true;
        return true;
    }
    else if (messageType == "msg_apply_force_zone")
    {
        message.loadVec3(_data->forceZoneVelocity);
        _data->triggerApplyForceZone = true;
        return true;
    }

    return false;
}

void Character::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);
    glm_vec3_copy(_data->position, _data->cpd->basePosition);
}

std::vector<std::string> getListOfWazaFnames()
{
    const std::string WAZA_DIRECTORY_PATH = "res/waza/";
    std::vector<std::string> wazaFnames;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(WAZA_DIRECTORY_PATH))
    {
        const auto& path = entry.path();
        if (std::filesystem::is_directory(path))
            continue;
        if (!path.has_extension() || path.extension().compare(".hwac") != 0)
            continue;
        auto relativePath = std::filesystem::relative(path, ".");
        wazaFnames.push_back(relativePath.string());  // @NOTE: that this line could be dangerous if there are any filenames or directory names that have utf8 chars or wchars in it
    }
    return wazaFnames;
}

void defaultRenderImGui(Character_XData* d)
{
    if (ImGui::CollapsingHeader("Tweak Props", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("modelSize", &d->modelSize);
        ImGui::InputInt("health", &d->health);
        ImGui::DragFloat("iframesTime", &d->iframesTime);
        ImGui::DragFloat("iframesTimer", &d->iframesTimer);
        
        int32_t knockbackModeI = (int32_t)d->knockbackMode;
        ImGui::Text(("knockbackMode: " + std::to_string(knockbackModeI)).c_str());
        ImGui::DragFloat("knockedbackTime", &d->knockedbackTime);
        ImGui::DragFloat("knockedbackTimer", &d->knockedbackTimer);

        ImGui::DragFloat("attackTwitchAngleReturnSpeed", &d->attackTwitchAngleReturnSpeed);
        if (d->uiMaterializeItem)
        {
            ImGui::DragFloat3("uiMaterializeItem->renderPosition", d->uiMaterializeItem->renderPosition);
        }
        if (d->uiStamina)
        {
            ImGui::DragFloat3("uiStamina->renderPosition", d->uiStamina->renderPosition);
        }
        ImGui::InputInt("currentWeaponDurability", &d->currentWeaponDurability);
        ImGui::DragFloat("inputMaxXZSpeed", &d->inputMaxXZSpeed);
        ImGui::DragFloat("midairXZAcceleration", &d->midairXZAcceleration);
        ImGui::DragFloat("midairXZDeceleration", &d->midairXZDeceleration);
        ImGui::DragFloat("wazaHitTimescale", &d->wazaHitTimescale);
        ImGui::DragFloat("wazaHitTimescaleOnHit", &d->wazaHitTimescaleOnHit);
        ImGui::DragFloat("wazaHitTimescaleReturnToOneSpeed", &d->wazaHitTimescaleReturnToOneSpeed);
    }

    if (ImGui::CollapsingHeader("Item Drops", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Harvestable item
        ImGui::Text("Harvestable item drops");
        ImGui::SameLine();
        if (ImGui::Button("Add..##Harvestable Item Drop"))
            ImGui::OpenPopup("add_harvestable_popup");
        if (ImGui::BeginPopup("add_harvestable_popup"))
        {
            for (size_t i = 0; i < globalState::getNumHarvestableItemIds(); i++)
            {
                if (ImGui::Button(globalState::getHarvestableItemByIndex(i)->name.c_str()))
                {
                    d->harvestableItemsIdsToSpawnAfterDeath.push_back(i);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        for (size_t i = 0; i < d->harvestableItemsIdsToSpawnAfterDeath.size(); i++)
        {
            size_t id = d->harvestableItemsIdsToSpawnAfterDeath[i];
            ImGui::Text(globalState::getHarvestableItemByIndex(id)->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button(("X##HIITSAD" + std::to_string(i)).c_str()))
            {
                d->harvestableItemsIdsToSpawnAfterDeath.erase(d->harvestableItemsIdsToSpawnAfterDeath.begin() + i);
                break;
            }
        }

        // Scannable item
        ImGui::Text("Scannable item drops");
        ImGui::SameLine();
        if (ImGui::Button("Add..##Scannable Item Drop"))
            ImGui::OpenPopup("add_scannable_popup");
        if (ImGui::BeginPopup("add_scannable_popup"))
        {
            for (size_t i = 0; i < globalState::getNumScannableItemIds(); i++)
            {
                if (ImGui::Button(globalState::getAncientWeaponItemByIndex(i)->name.c_str()))
                {
                    d->scannableItemsIdsToSpawnAfterDeath.push_back(i);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        for (size_t i = 0; i < d->scannableItemsIdsToSpawnAfterDeath.size(); i++)
        {
            size_t id = d->scannableItemsIdsToSpawnAfterDeath[i];
            ImGui::Text(globalState::getAncientWeaponItemByIndex(id)->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button(("X##SIITSAD" + std::to_string(i)).c_str()))
            {
                d->scannableItemsIdsToSpawnAfterDeath.erase(d->scannableItemsIdsToSpawnAfterDeath.begin() + i);
                break;
            }
        }
    }

    ImGui::Separator();

    // Enter into waza view/edit mode.
    static std::vector<std::string> listOfWazas;
    if (ImGui::Button("Open Waza in Editor.."))
    {
        listOfWazas = getListOfWazaFnames();
        ImGui::OpenPopup("open_waza_popup");
    }
    if (ImGui::BeginPopup("open_waza_popup"))
    {
        for (auto& path : listOfWazas)
            if (ImGui::Button(("Open \"" + path + "\"").c_str()))
            {
                d->attackWazaEditor.isEditingMode = true;
                d->attackWazaEditor.triggerRecalcWazaCache = true;
                d->attackWazaEditor.triggerRecalcHitscanLaunchVelocityCache = true;
                d->attackWazaEditor.triggerRecalcSelfVelocitySimCache = true;
                d->attackWazaEditor.preEditorAnimatorSpeedMultiplier = d->characterRenderObj->animator->getUpdateSpeedMultiplier();
                d->characterRenderObj->animator->setUpdateSpeedMultiplier(0.0f);

                d->attackWazaEditor.editingWazaFname = path;

                d->attackWazaEditor.editingWazaSet.clear();
                initWazaSetFromFile(d->attackWazaEditor.editingWazaSet, d->attackWazaEditor.editingWazaFname);

                d->attackWazaEditor.wazaIndex = 0;
                d->attackWazaEditor.currentTick = 0;
                ImGui::CloseCurrentPopup();
                break;
            }
        ImGui::EndPopup();
    }
}

void updateHitscanLaunchVeloRelPosExportString(Character_XData* d)
{
    auto& lv = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanLaunchVelocity;
    auto& rp = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanLaunchRelPosition;
    d->attackWazaEditor.hitscanLaunchVelocityExportString =
        "hs_launch_velocity " +
        std::to_string(lv[0]) + "," +
        std::to_string(lv[1]) + "," +
        std::to_string(lv[2]) + "\n" +
        "hs_rel_position    " +
        std::to_string(rp[0]) + "," +
        std::to_string(rp[1]) + "," +
        std::to_string(rp[2]) +
        (d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanLaunchRelPositionIgnoreY ?
            "    ignore_y" :
            "");
    d->attackWazaEditor.triggerRecalcHitscanLaunchVelocityCache = true;
}

void attackWazaEditorRenderImGui(Character_XData* d)
{
    if (ImGui::Button("Exit Waza Editor"))
    {
        d->attackWazaEditor.isEditingMode = false;
        d->characterRenderObj->animator->setUpdateSpeedMultiplier(d->attackWazaEditor.preEditorAnimatorSpeedMultiplier);
        // @TODO: reset animator and asm to default/root animation state.
        return;
    }

    ImGui::SameLine();
    if (ImGui::Button("Select Waza in Set.."))
        ImGui::OpenPopup("open_waza_in_set_popup");
    if (ImGui::BeginPopup("open_waza_in_set_popup"))
    {
        for (size_t i = 0; i < d->attackWazaEditor.editingWazaSet.size(); i++)
        {
            Character_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[i];
            if (ImGui::Button(aw.wazaName.c_str()))
            {
                // Change waza within set to edit.
                d->attackWazaEditor.wazaIndex = i;
                d->attackWazaEditor.currentTick = 0;
                d->attackWazaEditor.triggerRecalcWazaCache = true;
                d->attackWazaEditor.triggerRecalcHitscanLaunchVelocityCache = true;
                d->attackWazaEditor.triggerRecalcSelfVelocitySimCache = true;

                d->attackWazaEditor.hitscanLaunchVelocityExportString = "";
                d->attackWazaEditor.hitscanSetExportString = "";
                d->attackWazaEditor.vacuumSuckInExportString = "";
                d->attackWazaEditor.forceZoneExportString = "";
                ImGui::CloseCurrentPopup();
                break;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    ImGui::Text(d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].wazaName.c_str());  // Waza Name

    int32_t currentTickCopy = (int32_t)d->attackWazaEditor.currentTick;
    if (ImGui::SliderInt("Waza Tick", &currentTickCopy, d->attackWazaEditor.minTick, d->attackWazaEditor.maxTick))
    {
        d->attackWazaEditor.currentTick = (int16_t)currentTickCopy;
        d->attackWazaEditor.triggerRecalcWazaCache = true;
    }

    ImGui::Text("Bake hitscan with waza");
    ImGui::DragFloat2("Hitscan-based blade start end", d->attackWazaEditor.bladeDistanceStartEnd);
    ImGui::InputText("Hitscan-based bone", &d->attackWazaEditor.bladeBoneName_dirty);
    if (d->attackWazaEditor.bladeBoneName_dirty != d->attackWazaEditor.bladeBoneName)
    {
        ImGui::SameLine();
        if (ImGui::Button("Change!##Hitscan-based bone name"))
        {
            d->attackWazaEditor.bladeBoneName = d->attackWazaEditor.bladeBoneName_dirty;
        }
    }
    if (ImGui::Button("Set baking hitscan range start"))
        d->attackWazaEditor.bakeHitscanStartTick = d->attackWazaEditor.currentTick;
    if (ImGui::Button("Set baking hitscan range end"))
        d->attackWazaEditor.bakeHitscanEndTick = d->attackWazaEditor.currentTick;

    ImGui::BeginDisabled(d->attackWazaEditor.bakeHitscanStartTick < 0 || d->attackWazaEditor.bakeHitscanEndTick < 0 || d->attackWazaEditor.bakeHitscanStartTick >= d->attackWazaEditor.bakeHitscanEndTick);
    if (ImGui::Button(("Bake hitscans (range: [" + std::to_string(d->attackWazaEditor.bakeHitscanStartTick) + ", " + std::to_string(d->attackWazaEditor.bakeHitscanEndTick) + "])").c_str()))
    {
        d->attackWazaEditor.triggerBakeHitscans = true;
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    if (!d->attackWazaEditor.hitscanLaunchVelocitySimCache.empty() &&
        !d->attackWazaEditor.selfVelocitySimCache.empty())
    {
        ImGui::SliderInt(
            "Launch/Self Velocity Awase Step",
            &d->attackWazaEditor.hitscanLaunchAndSelfVelocityAwaseIndex,
            0,
            (int32_t)std::min(d->attackWazaEditor.hitscanLaunchVelocitySimCache.size(), d->attackWazaEditor.selfVelocitySimCache.size())
        );
    }

    if (ImGui::DragFloat3("Launch Velocity", d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanLaunchVelocity) ||
        ImGui::DragFloat3("Launch Rel Position", d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanLaunchRelPosition) ||
        ImGui::Checkbox("Ignore Rel Position Y", &d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanLaunchRelPositionIgnoreY))
    {
        updateHitscanLaunchVeloRelPosExportString(d);
    }

    auto& vsi = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].vacuumSuckIn;
    ImGui::Separator();
    ImGui::Checkbox("Enable Vacuum Suck In", &vsi.enabled);
    if (vsi.enabled)
    {
        if (ImGui::DragFloat3("Vacuum Suck In Position", vsi.position) ||
            ImGui::DragFloat("Vacuum Suck In Radius", &vsi.radius) ||
            ImGui::DragFloat("Vacuum Suck In Strength", &vsi.strength))
        {
            d->attackWazaEditor.vacuumSuckInExportString =
                "vacuum_suck_in     " +
                std::to_string(vsi.position[0]) + "," +
                std::to_string(vsi.position[1]) + "," +
                std::to_string(vsi.position[2]) + "    " +
                std::to_string(vsi.radius) + "    " +
                std::to_string(vsi.strength);
            vsi.enabled = true;
        }
    }

    auto& fz = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].forceZone;
    ImGui::Separator();
    bool updateFZ = false;
    updateFZ |= ImGui::Checkbox("Enable Force Zone", &fz.enabled);
    if (fz.enabled)
    {
        updateFZ |= ImGui::DragFloat3("Force Zone origin", fz.origin);
        updateFZ |= ImGui::DragFloat3("Force Zone bounds", fz.bounds);
        updateFZ |= ImGui::DragFloat3("Force Zone forceVelocity", fz.forceVelocity);
        int32_t timeFrom = fz.timeFrom;
        int32_t timeTo = fz.timeTo;
        updateFZ |= ImGui::DragInt("Force Zone time from", &timeFrom);
        updateFZ |= ImGui::DragInt("Force Zone time to", &timeTo);
        if (updateFZ)
        {
            fz.timeFrom = timeFrom;
            fz.timeTo = timeTo;

            d->attackWazaEditor.forceZoneExportString =
                "force_zone         " +
                std::to_string(fz.origin[0]) + "," +
                std::to_string(fz.origin[1]) + "," +
                std::to_string(fz.origin[2]) + "    " +
                std::to_string(fz.bounds[0]) + "," +
                std::to_string(fz.bounds[1]) + "," +
                std::to_string(fz.bounds[2]) + "    " +
                std::to_string(fz.forceVelocity[0]) + "," +
                std::to_string(fz.forceVelocity[1]) + "," +
                std::to_string(fz.forceVelocity[2]) + "    " +
                std::to_string(fz.timeFrom) + "    " +
                std::to_string(fz.timeTo);
        }
    }

    if (!d->attackWazaEditor.hitscanLaunchVelocityExportString.empty())
    {
        ImGui::Separator();
        ImGui::Text("Launch Velocity Export String");
        ImGui::InputTextMultiline("##Attack Waza Launch Velocity Export string copying area", &d->attackWazaEditor.hitscanLaunchVelocityExportString, ImVec2(512, ImGui::GetTextLineHeight() * 5));
    }

    if (!d->attackWazaEditor.hitscanSetExportString.empty())
    {
        ImGui::Separator();

        ImGui::Text("Hitscan Export String");
        ImGui::InputTextMultiline("##Attack Waza Export string copying area", &d->attackWazaEditor.hitscanSetExportString, ImVec2(512, ImGui::GetTextLineHeight() * 16), ImGuiInputTextFlags_AllowTabInput);
    }

    if (!d->attackWazaEditor.vacuumSuckInExportString.empty())
    {
        ImGui::Separator();

        ImGui::Text("Vacuum Suckin Export String");
        ImGui::InputTextMultiline("##Vacuum suckin export string copying area", &d->attackWazaEditor.vacuumSuckInExportString, ImVec2(512, ImGui::GetTextLineHeight() * 5));
    }

    if (!d->attackWazaEditor.forceZoneExportString.empty())
    {
        ImGui::Separator();

        ImGui::Text("Force Zone Export String");
        ImGui::InputTextMultiline("##Force zone export string copying area", &d->attackWazaEditor.forceZoneExportString, ImVec2(512, ImGui::GetTextLineHeight() * 5));
    }
}

void Character::renderImGui()
{
    if (_data->attackWazaEditor.isEditingMode)
        attackWazaEditorRenderImGui(_data);
    else
        defaultRenderImGui(_data);
}
