#include "pch.h"

#include "SimulationCharacter.h"

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
#ifdef _DEVELOP
#include "HotswapResources.h"
#endif


std::string CHARACTER_TYPE_PLAYER = "PLAYER";
std::string CHARACTER_TYPE_NPC = "NPC";

struct SimulationCharacter_XData
{
    std::string characterType = CHARACTER_TYPE_PLAYER;

    RenderObjectManager*     rom;
    Camera*                  camera;
    RenderObject*            characterRenderObj;
    RenderObject*            handleRenderObj;
    RenderObject*            weaponRenderObj;
    std::string              weaponAttachmentJointName;
    RenderObject*            chirpySphereRenderObj;

    physengine::CapsulePhysicsData* cpd;
    std::vector<vec3s> basePoints;
    std::vector<vec3s> extrapolatingBasePoints;

    struct BackendMovementInputState
    {
        vec3    inputVelocity = GLM_VEC3_ZERO_INIT;
        float_t verticalVelocity = 0.0f;
        float_t cosMaxGroundSlopeAngle = std::cosf(glm_rad(46.0f));
        float_t cosMaxCeilingSlopeAngle = std::cosf(glm_rad(35.0f));
        float_t stickToGroundMaxDelta = 0.5f;
        vec3    gravityDirection;
        float_t gravityMagnitude;

        bool isGrounded;
        bool hitCeiling;
        bool attemptStickToGround = false;

        BackendMovementInputState()
        {
            physengine::getWorldGravity(gravityDirection);
            glm_vec3_negate(gravityDirection);  // So that `verticalVelocity` is down when negative.
            gravityMagnitude = glm_vec3_norm(gravityDirection);
            glm_vec3_normalize(gravityDirection);
        }
    } bmis;

    struct FrontendMovementInputState
    {
        float_t maxSpeed = 7.5f;
        float_t currentSpeed = 0.0f;
        vec2 groundedAccelDecel = { 30.0f, 28.0f };
        vec2 negGroundedAccelDecel = { 60.0f, 56.0f };
        vec2 airborneAccelDecel = { 30.0f, 10.0f };
        float_t turningSpeed = glm_rad(270.0f);

        enum class MovementType
        {
            STANDING = 0,
            RUNNING,
            FALLING,
            JUMPING,
        } currentMvtType = MovementType::STANDING;
        int32_t mvtTypeSteps = 0;

        void setMvtType(MovementType mt)
        {
            currentMvtType = mt;
            mvtTypeSteps = 0;
        }
    } fmis;

    struct EXPERIMENTAL__ShouldbeInSeparateClassCombatStateMachine
    {
        int32_t currentBeat = 0;
        int32_t tempo = 20;                       // Number of simulation ticks for one beat (40 ticks per second, 20 tempo: twice per second, i.e. 120bpm)

        ivec2 inputAcceptableRange = { -5, 10 };  // @NOTE: based off this data: https://www.desmos.com/calculator/gttn6iwzy6

        enum class CombatState
        {
            IDLE = 0,
            WEAPON_CHARGING,
            ATTACK,
        };
        CombatState playerCombatState, playerNextCombatState;
        CombatState enemyCombatState, enemyNextCombatState;

        bool playerInputtedActionThisBeat = false;

        bool interactionProcessed = false;
    } csm;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


    struct MovingPlatformAttachment
    {
        enum class AttachmentStage
        {
            NO_ATTACHMENT = 0,
            INITIAL_ATTACHMENT,  // Initial attachment position is calculated.
            FIRST_DELTA_ATTACHMENT,  // Got delta position for first step attaching to the platform. Don't subtract from the velocity but add to it.
            RECURRING_ATTACHMENT  // First step was applied to velocity at this point. Add and subtract from velocity.
        };
        AttachmentStage attachmentStage = AttachmentStage::NO_ATTACHMENT;
        JPH::BodyID attachedBodyId;
        JPH::RVec3 attachmentPositionWorld;
        JPH::Vec3 attachmentPositionLocal;
        float_t   attachmentYAxisAngularVelocity;
        vec3      nextDeltaPosition;
        vec3      prevDeltaPosition;
        bool      attachmentIsStale = true;  // If `reportPhysicsContact` doesn't come in to reset this to `false` then the attachment stage will get reset.
    } movingPlatformAttachment;

    textmesh::TextMesh* uiMaterializeItem;  // @NOTE: This is a debug ui thing. In the real thing, I'd really want there to be in the bottom right the ancient weapon handle pointing vertically with the materializing item in a wireframe with a mysterious blue hue at the end of the handle, and when the item does get materialized, it becomes a rendered version.
    globalState::ScannableItemOption* materializedItem = nullptr;
    int32_t currentWeaponDurability;

    textmesh::TextMesh* uiStamina;
    struct StaminaData
    {
        float_t currentStamina;
        int16_t maxStamina = 10000;
        float_t refillTime = 0.5f;  // Wait this time before starting to refill stamina.
        float_t refillTimer = 0.0f;
        float_t changedTime = 0.5f;  // Wait this time before disappearing after a stamina change occurred.
        float_t changedTimer = 0.0f;
        float_t refillRate = 50.0f;

        float_t depletionOverflow = 0.0f;
        float_t doRemove1HealthThreshold = 10.0f;
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
    bool        wazaVelocityFirstStep = false;
    int16_t     wazaTimer = 0;  // Used for timing chains and hitscans.
    float_t     wazaHitTimescale = 1.0f;
    float_t     wazaHitTimescaleOnHit = 0.01f;
    float_t     wazaHitTimescaleReturnToOneSpeed = 1500.0f;

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
#ifdef _DEVELOP
    bool    disableInput = false;  // @DEBUG for level editor
#endif
    float_t attackTwitchAngle = 0.0f;
    float_t attackTwitchAngleReturnSpeed = 3.0f;
    vec3    prevGroundNormal = GLM_VEC3_ZERO_INIT;
    bool    prevGroundNormalSet = false;
    int32_t TEMPASDFASDFTICKSMIDAIR = 0;
    bool    prevIsGrounded = false;
    bool    prevPrevIsGrounded = false;

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
    bool    prevPerformedJump = false;

    float_t inputMaxXZSpeed = 7.5f;
    float_t midairXZAcceleration = 1.0f;
    float_t midairXZDeceleration = 0.25f;
    float_t knockedbackGroundedXZDeceleration = 0.5f;
    float_t recoveryGroundedXZDeceleration = 0.75f;

    bool isTargetingOpponentObject = false;
    std::vector<int32_t> auraSfxChannelIds;
    float_t auraTimer = 0.0f;
    float_t auraPersistanceTime = 1.0f;

    // Tweak Props
    vec3 position;
    float_t facingDirection = 0.0f;
    float_t modelSize = 0.3f;
    float_t jumpHeight = 15.0f;
    
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

inline bool isPlayer(SimulationCharacter_XData* d) { return d->characterType == CHARACTER_TYPE_PLAYER; }

void processOutOfHealth(EntityManager* em, Entity* e, SimulationCharacter_XData* d)
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

void pushPlayerNotification(const std::string& message, SimulationCharacter_XData* d)
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

std::string getUIMaterializeItemText(SimulationCharacter_XData* d)
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

std::string getStaminaText(SimulationCharacter_XData* d)
{
    return "Stamina: " + std::to_string((int32_t)std::round(d->staminaData.currentStamina)) + "/" + std::to_string(d->staminaData.maxStamina);
}

void changeStamina(SimulationCharacter_XData* d, float_t amount, bool allowDepletionOverflow)
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

void processAttack(SimulationCharacter_XData* d)
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

void processRelease(SimulationCharacter_XData* d)
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

void loadDataFromLine(SimulationCharacter_XData::AttackWaza& newWaza, const std::string& command, const std::vector<std::string>& params)
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
        SimulationCharacter_XData::AttackWaza::VelocityDecaySetting newVelocityDecaySetting;
        newVelocityDecaySetting.velocityDecay = std::stof(params[0]);
        newVelocityDecaySetting.executeAtTime = std::stoi(params[1]);
        newWaza.velocityDecaySettings.push_back(newVelocityDecaySetting);
    }
    else if (command == "velocity")
    {
        SimulationCharacter_XData::AttackWaza::VelocitySetting newVelocitySetting;
        vec3 velo;
        parseVec3CommaSeparated(params[0], velo);
        glm_vec3_copy(velo, newVelocitySetting.velocity);
        newVelocitySetting.executeAtTime = std::stoi(params[1]);
        newWaza.velocitySettings.push_back(newVelocitySetting);
    }
    else if (command == "hitscan")
    {
        SimulationCharacter_XData::AttackWaza::HitscanFlowNode newHitscanNode;
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
        SimulationCharacter_XData::AttackWaza::Chain newChain;
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

SimulationCharacter_XData::AttackWaza* getWazaPtrFromName(std::vector<SimulationCharacter_XData::AttackWaza>& wazas, const std::string& wazaName)
{
    if (wazaName == "NULL")  // Special case.
        return nullptr;

    for (SimulationCharacter_XData::AttackWaza& waza : wazas)
    {
        if (waza.wazaName == wazaName)
            return &waza;
    }

    std::cerr << "[WAZA LOADING]" << std::endl
        << "ERROR: Waza with name \"" << wazaName << "\" was not found (`getWazaPtrFromName`)." << std::endl;
    return nullptr;
}

SimulationCharacter_XData::AttackWaza::WazaInput getInputEnumFromName(const std::string& inputName)
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
        return SimulationCharacter_XData::AttackWaza::WazaInput(0);
    }

    return SimulationCharacter_XData::AttackWaza::WazaInput(enumValue);
}

void initWazaSetFromFile(std::vector<SimulationCharacter_XData::AttackWaza>& wazas, const std::string& fname)
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
    SimulationCharacter_XData::AttackWaza newWaza;
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
                newWaza = SimulationCharacter_XData::AttackWaza();
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
        newWaza = SimulationCharacter_XData::AttackWaza();
    }

    //
    // Bake pointers into string references.
    //
    for (SimulationCharacter_XData::AttackWaza& waza : wazas)
    {
        if (waza.wazaName == "NULL")
        {
            std::cerr << "[WAZA LOADING]" << std::endl
                << "ERROR: You can't name a waza state \"NULL\"... it's a keyword!!! Aborting." << std::endl;
            break;
        }

        if (waza.entranceInputParams.inputName != "NULL")
            waza.entranceInputParams.input = getInputEnumFromName(waza.entranceInputParams.inputName);

        for (SimulationCharacter_XData::AttackWaza::Chain& chain : waza.chains)
        {
            chain.nextWazaPtr = getWazaPtrFromName(wazas, chain.nextWazaName);
            chain.input = getInputEnumFromName(chain.inputName);
        }
        waza.onHoldCancelWazaPtr = getWazaPtrFromName(wazas, waza.onHoldCancelWazaName);
        waza.onDurationPassedWazaPtr = getWazaPtrFromName(wazas, waza.onDurationPassedWazaName);
    }
}

// @TODO: delete this once not needed. Well, it's not needed rn bc it's commented out, but once the knowledge isn't needed anymore, delete this.  -Timo 2023/09/19
// void processWeaponAttackInput(SimulationCharacter_XData* d)
// {
//     SimulationCharacter_XData::AttackWaza* nextWaza = nullptr;
//     bool attackFailed = false;
//     int16_t staminaCost;

//     if (d->currentWaza == nullptr)
//     {
//         // By default start at the root waza.
//         // @NOCHECKIN: @FIXME: collect the inputs natively right here instead of having to wait for LMB input or whatever to trigger this function.
//         // if (input::keyTargetPressed)
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

//////////////// SimulationCharacter_XData::AttackWaza::WazaInputType processInputForKey(SimulationCharacter_XData::PressedState currentInput, uint8_t ticksToHold, uint8_t ticksToClearState, uint8_t invalidTicksToClearState, SimulationCharacter_XData::GestureInputState& inoutIS)
//////////////// {
////////////////     SimulationCharacter_XData::AttackWaza::WazaInputType wit = SimulationCharacter_XData::AttackWaza::WazaInputType::NONE;
//////////////// 
////////////////     // Increment ticks.
////////////////     switch (currentInput)
////////////////     {
////////////////         case SimulationCharacter_XData::PressedState::PRESSED:
////////////////             inoutIS.numPressTicks++;
////////////////             break;
//////////////// 
////////////////         case SimulationCharacter_XData::PressedState::RELEASED:
////////////////             inoutIS.numReleaseTicks++;
////////////////             break;
//////////////// 
////////////////         case SimulationCharacter_XData::PressedState::INVALID:
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
////////////////     else if (currentInput != SimulationCharacter_XData::PressedState::INVALID)
////////////////     {
////////////////         if (currentInput == SimulationCharacter_XData::PressedState::RELEASED && inoutIS.numPressTicks > 0 && inoutIS.numPressTicks <= ticksToHold)
////////////////         {
////////////////             // `press` was executed. (This is really a click.)
////////////////             if (inoutIS.completedPress)
////////////////                 wit = SimulationCharacter_XData::AttackWaza::WazaInputType::DOUBLECLICK;
////////////////             else
////////////////             {
////////////////                 inoutIS.completedPress = true;
////////////////                 wit = SimulationCharacter_XData::AttackWaza::WazaInputType::PRESS;
////////////////             }
////////////////             inoutIS.numPressTicks = 0;
////////////////         }
////////////////         else if (currentInput == SimulationCharacter_XData::PressedState::PRESSED && inoutIS.numPressTicks == ticksToHold)
////////////////         {
////////////////             // `hold` was executed.
////////////////             if (inoutIS.completedPress)
////////////////                 wit = SimulationCharacter_XData::AttackWaza::WazaInputType::DOUBLEHOLD;
////////////////             else
////////////////                 wit = SimulationCharacter_XData::AttackWaza::WazaInputType::HOLD;
////////////////         }
////////////////         else if (currentInput == SimulationCharacter_XData::PressedState::RELEASED && inoutIS.numPressTicks > ticksToHold)
////////////////         {
////////////////             // `release` was executed.
////////////////             wit = SimulationCharacter_XData::AttackWaza::WazaInputType::RELEASE;
////////////////             inoutIS.numPressTicks = 0;
////////////////         }
////////////////     }
//////////////// 
////////////////     // Clear ticks.
////////////////     switch (currentInput)
////////////////     {
////////////////         case SimulationCharacter_XData::PressedState::PRESSED:
////////////////             inoutIS.numReleaseTicks = 0;
////////////////             inoutIS.numInvalidTicks = 0;
////////////////             break;
//////////////// 
////////////////         case SimulationCharacter_XData::PressedState::RELEASED:
////////////////             inoutIS.numPressTicks = 0;
////////////////             inoutIS.numInvalidTicks = 0;
////////////////             break;
//////////////// 
////////////////         case SimulationCharacter_XData::PressedState::INVALID:
////////////////             // Do nothing.
////////////////             break;
////////////////     }
//////////////// 
////////////////     return wit;
//////////////// }

inline SimulationCharacter_XData::PressedState pressedStateSingle(const bool& isPressed)
{
    return isPressed ? SimulationCharacter_XData::PressedState::PRESSED : SimulationCharacter_XData::PressedState::RELEASED;
}

inline SimulationCharacter_XData::PressedState pressedStateCombo(const std::vector<SimulationCharacter_XData::PressedState>& singleStates)
{
    SimulationCharacter_XData::PressedState originalState = singleStates[0];
    for (size_t i = 1; i < singleStates.size(); i++)
        if (originalState != singleStates[i])
            return SimulationCharacter_XData::PressedState::INVALID;
    return originalState;
}

// // @DEBUG
// std::string totootototNOCHECKIN(SimulationCharacter_XData::AttackWaza::WazaInputType wit)
// {
//     switch (wit)
//     {
//         case SimulationCharacter_XData::AttackWaza::WazaInputType::NONE: return "NONE";
//         case SimulationCharacter_XData::AttackWaza::WazaInputType::PRESS: return "PRESS";
//         case SimulationCharacter_XData::AttackWaza::WazaInputType::HOLD: return "HOLD";
//         case SimulationCharacter_XData::AttackWaza::WazaInputType::RELEASE: return "RELEASE";
//         case SimulationCharacter_XData::AttackWaza::WazaInputType::DOUBLECLICK: return "DOUBLECLICK";
//         case SimulationCharacter_XData::AttackWaza::WazaInputType::DOUBLEHOLD: return "DOUBLEHOLD";
//     }

//     return "INVALID (UNKNOWN): " + (int32_t)wit;
// }

// size_t jal = 0;
// ////////

inline SimulationCharacter_XData::AttackWaza::WazaInput inputTypeToWazaInput(int32_t keyType, SimulationCharacter_XData::PressedState inputType)
{
    // @NOTE: this function assumes that `inputType` is >=1.
    return SimulationCharacter_XData::AttackWaza::WazaInput(
        3 * ((int32_t)inputType - 1) + keyType + 1  // @COPYPASTA
    );
}

#define MAX_SIMULTANEOUS_WAZA_INPUTS 8

void processInputForWaza(SimulationCharacter_XData* d, SimulationCharacter_XData::AttackWaza::WazaInput* outWazaInputs, size_t& outNumInputs)
{
    // @NOCHECKIN: I don't like this system for doing the key combinations. `release` events don't work.
    //             Figure out some way for the key combination press, release, and hold, etc. to work.
    //             I think that keeping the 3 states for `currentInput` is good, but making the single input
    //             processing just use the pressed/released states only and not get changed/filtered out.
    //             Then, use the invalid/pressed/released for combination moves. Maybe HOLD/DOUBLEHOLD is really
    //             the only event needing to have priority in combo inputs.  -Timo 2023/09/17
    // @REPLY: I think that this new system is what is needed.  -Timo 2023/09/17
    // @REPLY: I think that the previous new system sucked and now I'm implementing a new system i like (have HWAC file take care of timing).  -Timo 2023/09/22
    SimulationCharacter_XData::PressedState input_x = pressedStateSingle(input::simInputSet().attack.holding);
    SimulationCharacter_XData::PressedState input_a = pressedStateSingle(input::simInputSet().jump.holding);
    SimulationCharacter_XData::PressedState input_xa = pressedStateCombo({ input_x, input_a });

    // Fill in all the waza inputs. (@NOTE: start with key combinations and check inputs that are highest priority first)
    outNumInputs = 0;
    if (input_xa > SimulationCharacter_XData::PressedState::INVALID &&
        input_xa != d->prevInput_xa)
        outWazaInputs[outNumInputs++] = inputTypeToWazaInput(2, input_xa);
    if (input_x > SimulationCharacter_XData::PressedState::INVALID &&
        input_x != d->prevInput_x)
        outWazaInputs[outNumInputs++] = inputTypeToWazaInput(0, input_x);
    if (input_a > SimulationCharacter_XData::PressedState::INVALID &&
        input_a != d->prevInput_a)
        outWazaInputs[outNumInputs++] = inputTypeToWazaInput(1, input_a);

    d->prevInput_xa = input_xa;
    d->prevInput_x = input_x;
    d->prevInput_a = input_a;
}

struct NextWazaPtr
{
    SimulationCharacter_XData::AttackWaza* nextWaza = nullptr;
    bool set = false;
};

void processWazaInput(SimulationCharacter_XData* d, SimulationCharacter_XData::AttackWaza::WazaInput* wazaInputs, size_t numInputs, NextWazaPtr& inoutNextWaza)
{
    std::string movementState = d->prevIsGrounded ? "grounded" : (d->isMidairUpsideDown ? "upsidedown" : "midair");

    std::cout << "MVT ST: " << movementState << std::endl;

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
        SimulationCharacter_XData::AttackWaza::WazaInput wazaInput = wazaInputs[i];
        if (wazaInput == SimulationCharacter_XData::AttackWaza::WazaInput::NONE)
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
                    // else if (chain.input == SimulationCharacter_XData::AttackWaza::WazaInput::HOLD_X ||
                    //     chain.input == SimulationCharacter_XData::AttackWaza::WazaInput::HOLD_A ||
                    //     chain.input == SimulationCharacter_XData::AttackWaza::WazaInput::HOLD_X_A)
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

void processWazaUpdate(SimulationCharacter_XData* d, EntityManager* em, float_t simDeltaTime, const std::string& myGuid, NextWazaPtr& inoutNextWaza, bool& inoutTurnOnAura)
{
    //
    // Deplete stamina
    //
    if (d->currentWaza->staminaCostHold > 0 &&
        (d->currentWaza->staminaCostHoldTimeFrom < 0 || d->wazaTimer >= d->currentWaza->staminaCostHoldTimeFrom) &&
        (d->currentWaza->staminaCostHoldTimeTo < 0 || d->wazaTimer <= d->currentWaza->staminaCostHoldTimeTo))
    {
        changeStamina(d, -d->currentWaza->staminaCostHold * simDeltaTime, true);
        inoutTurnOnAura = true;
    }

    //
    // Execute all velocity decay settings.
    //
    for (SimulationCharacter_XData::AttackWaza::VelocityDecaySetting& vds : d->currentWaza->velocityDecaySettings)
        if (vds.executeAtTime == d->wazaTimer)
        {
            d->wazaVelocityDecay = vds.velocityDecay;
            break;
        }

    //
    // Execute all velocity settings corresponding to the timer.
    //
    for (SimulationCharacter_XData::AttackWaza::VelocitySetting& velocitySetting : d->currentWaza->velocitySettings)
        if (velocitySetting.executeAtTime == d->wazaTimer)
        {
            glm_vec3_copy(velocitySetting.velocity, d->wazaVelocity);
            d->wazaVelocityFirstStep = true;
            break;
        }

    //
    // Execute all hitscans that need to be executed in the timeline.
    //
    size_t hitscanLayer = physengine::getCollisionLayer("HitscanInteractible");
    vec3 offset(0.0f, -physengine::getLengthOffsetToBase(*d->cpd), 0.0f);
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

                vec3 directionAndMagnitude;  // https://www.youtube.com/watch?v=A05n32Bl0aY
                glm_vec3_sub(pt2, pt1, directionAndMagnitude);

                std::string hitGuid;
                if (physengine::raycast(pt1, directionAndMagnitude, hitGuid))
                {
                    // Successful hitscan!
                    float_t attackLvl =
                        (float_t)(d->currentWeaponDurability > 0 ?
                            d->materializedItem->weaponStats.attackPower :
                            d->materializedItem->weaponStats.attackPowerWhenDulled);

                    if (hitGuid == myGuid)
                        continue;  // Ignore if hitscan to self

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
                    glm_vec3_add(offset, setPosition, setPosition);
                    ds.dumpVec3(setPosition);

                    float_t ignoreYF = (float_t)d->currentWaza->hitscanLaunchRelPositionIgnoreY;
                    ds.dumpFloat(ignoreYF);

                    DataSerialized dsd = ds.getSerializedData();
                    if (em->sendMessage(hitGuid, dsd))
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
                glm_vec3_sub(suckPositionWS, otherCPD->currentCOMPosition, deltaPosition);
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
                glm_vec3_lerp(suckPositionWS, otherCPD->currentCOMPosition, t, midpt);
                if (glm_vec3_norm2(deltaPosition) < radius * radius)
                {
                    physengine::drawDebugVisLine(suckPositionWS, otherCPD->currentCOMPosition, physengine::DebugVisLineType::SUCCESS);
                    physengine::drawDebugVisLine(otherCPD->currentCOMPosition, midpt, physengine::DebugVisLineType::KIKKOARMY);
                }
                else
                {
                    physengine::drawDebugVisLine(suckPositionWS, midpt, physengine::DebugVisLineType::AUDACITY);
                    physengine::drawDebugVisLine(midpt, otherCPD->currentCOMPosition, physengine::DebugVisLineType::VELOCITY);
                }
            }

            // Force zone.
            if (forceZoneEnabled)
            {
                vec3 deltaPosition;
                glm_vec3_sub(forceZoneOriginWS, otherCPD->currentCOMPosition, deltaPosition);
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

void setWazaToCurrent(SimulationCharacter_XData* d, SimulationCharacter_XData::AttackWaza* nextWaza)
{
    std::cout << "[SET WAZA STATE]" << std::endl
        << "New state: " << (nextWaza != nullptr ? nextWaza->wazaName : "NULL") << std::endl;
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

SimulationCharacter::SimulationCharacter(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds) : Entity(em, ds), _data(new SimulationCharacter_XData())
{
    Entity::_enableSimulationUpdate = true;

    _data->rom = rom;
    _data->camera = camera;

    if (ds)
        load(*ds);

    _data->staminaData.currentStamina = (float_t)_data->staminaData.maxStamina;

    // Create physics character.
    bool useCCD = false;  // @NOTE: since there's the change to base movement off collide and slide, ccd needs to be turned off, at least during c&s-style movement.  //(isPlayer(_data));
    _data->cpd = physengine::createCharacter(getGUID(), _data->position, 0.25f, 1.4f, useCCD);  // Total height is 1.9, but r*2 is subtracted to get the capsule height (i.e. the line segment length that the capsule rides along)

    // Calculate base points.
    float_t b = std::sinf(glm_rad(45.0f));
    _data->basePoints = {
        vec3s{     std::cosf(glm_rad(0.0f)),      0.0f,        std::sinf(glm_rad(0.0f))   },
        vec3s{     std::cosf(glm_rad(45.0f)),     0.0f,        std::sinf(glm_rad(45.0f))  },
        vec3s{     std::cosf(glm_rad(90.0f)),     0.0f,        std::sinf(glm_rad(90.0f))  },
        vec3s{     std::cosf(glm_rad(135.0f)),    0.0f,        std::sinf(glm_rad(135.0f)) },
        vec3s{     std::cosf(glm_rad(180.0f)),    0.0f,        std::sinf(glm_rad(180.0f)) },
        vec3s{     std::cosf(glm_rad(225.0f)),    0.0f,        std::sinf(glm_rad(225.0f)) },
        vec3s{     std::cosf(glm_rad(270.0f)),    0.0f,        std::sinf(glm_rad(270.0f)) },
        vec3s{     std::cosf(glm_rad(315.0f)),    0.0f,        std::sinf(glm_rad(315.0f)) },
        vec3s{ b * std::cosf(glm_rad(0.0f)),     -b,       b * std::sinf(glm_rad(0.0f))   },
        vec3s{ b * std::cosf(glm_rad(60.0f)),    -b,       b * std::sinf(glm_rad(60.0f))  },
        vec3s{ b * std::cosf(glm_rad(120.0f)),   -b,       b * std::sinf(glm_rad(120.0f)) },
        vec3s{ b * std::cosf(glm_rad(180.0f)),   -b,       b * std::sinf(glm_rad(180.0f)) },
        vec3s{ b * std::cosf(glm_rad(240.0f)),   -b,       b * std::sinf(glm_rad(240.0f)) },
        vec3s{ b * std::cosf(glm_rad(300.0f)),   -b,       b * std::sinf(glm_rad(300.0f)) },
        vec3s{     0.0f,                         -1.0f,        0.0f                       },
    };

    // Calculate extrapolating base points (can be transformed with facing direction).
    _data->extrapolatingBasePoints = {
        vec3s{     std::cosf(glm_rad(0.0f)),      0.0f,        std::sinf(glm_rad(0.0f))   },
        vec3s{     std::cosf(glm_rad(45.0f)),     0.0f,        std::sinf(glm_rad(45.0f))  },
        vec3s{     std::cosf(glm_rad(90.0f)),     0.0f,        std::sinf(glm_rad(90.0f))  },
        vec3s{     std::cosf(glm_rad(135.0f)),    0.0f,        std::sinf(glm_rad(135.0f)) },
        vec3s{     std::cosf(glm_rad(180.0f)),    0.0f,        std::sinf(glm_rad(180.0f)) },
        vec3s{ b * std::cosf(glm_rad(0.0f)),     -b,       b * std::sinf(glm_rad(0.0f))   },
        vec3s{ b * std::cosf(glm_rad(60.0f)),    -b,       b * std::sinf(glm_rad(60.0f))  },
        vec3s{ b * std::cosf(glm_rad(120.0f)),   -b,       b * std::sinf(glm_rad(120.0f)) },
        vec3s{ b * std::cosf(glm_rad(180.0f)),   -b,       b * std::sinf(glm_rad(180.0f)) },
        vec3s{     0.0f,                         -1.0f,        0.0f                       },
    };

    // Scale base points to character sizing.
    for (auto& base : _data->basePoints)
    {
        glm_vec3_scale(base.raw, _data->cpd->radius, base.raw);
        glm_vec3_add(base.raw, vec3{ 0.0f, -_data->cpd->height * 0.5f, 0.0f}, base.raw);
    }
    for (auto& base : _data->extrapolatingBasePoints)
    {
        glm_vec3_scale(base.raw, _data->cpd->radius, base.raw);
        glm_vec3_add(base.raw, vec3{ 0.0f, -_data->cpd->height * 0.5f, 0.0f}, base.raw);
    }

    // Setup player ui elements and wazas.
    if (isPlayer(_data))
    {
        globalState::playerGUID = getGUID();
        globalState::playerPositionRef = &_data->cpd->currentCOMPosition;

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

    // Create render objects.
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
            "EventPlaySFXSmallJump", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_jump1.ogg",
                    "res/sfx/wip_jump2.ogg",
                });
            }
        },
        {
            "EventPlaySFXLargeJump", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_LSword_SwingFast1.wav",
                    "res/sfx/wip_LSword_SwingFast2.wav",
                    "res/sfx/wip_LSword_SwingFast3.wav",
                    "res/sfx/wip_LSword_SwingFast4.wav",
                    "res/sfx/wip_LSword_SwingFast5.wav",
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

    std::vector<vkglTF::Animator::AnimatorCallback> chirpSphereAnimatorCallbacks = {
        {
            "EventPlayChirpSFX", [&]() {
                // if (isPlayer(_data))
                //     AudioEngine::getInstance().playSoundFromList({
                //         "res/sfx/wip_metronome_tick.wav",
                //     });
            }
        },
    };

    vkglTF::Model* characterModel = _data->rom->getModel("SlimeGirl", this, [](){});
    vkglTF::Model* handleModel = _data->rom->getModel("Handle", this, [](){});
    vkglTF::Model* weaponModel = _data->rom->getModel("WingWeapon", this, [](){});
    vkglTF::Model* chirpSphereModel = _data->rom->getModel("ChirpAnimatedSphere", this, [](){});

    _data->rom->registerRenderObjects({
            {
                .model = characterModel,
                .animator = new vkglTF::Animator(characterModel, animatorCallbacks),
                .simTransformId = _data->cpd->simTransformId,
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
            /*{
                .model = chirpSphereModel,
                .animator = new vkglTF::Animator(chirpSphereModel, chirpSphereAnimatorCallbacks),
                .simTransformId = _data->cpd->simTransformId,
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = getGUID(),
            },*/
        },
        { &_data->characterRenderObj, &_data->handleRenderObj, &_data->weaponRenderObj, /*&_data->chirpySphereRenderObj*/ }
    );

    glm_mat4_identity(_data->characterRenderObj->simTransformOffset);
    glm_translate(_data->characterRenderObj->simTransformOffset, vec3{ 0.0f, -physengine::getLengthOffsetToBase(*_data->cpd), 0.0f });
    glm_scale(_data->characterRenderObj->simTransformOffset, vec3{ _data->modelSize, _data->modelSize, _data->modelSize });

    // @HARDCODED: there should be a sensing algorithm to know which lightgrid to assign itself to.
    for (auto& inst : _data->characterRenderObj->calculatedModelInstances)
        inst.voxelFieldLightingGridID = 1;
    for (auto& inst : _data->handleRenderObj->calculatedModelInstances)
        inst.voxelFieldLightingGridID = 1;
    for (auto& inst : _data->weaponRenderObj->calculatedModelInstances)
        inst.voxelFieldLightingGridID = 1;
}

SimulationCharacter::~SimulationCharacter()
{
    hotswapres::removeOwnedCallbacks(this);

    if (_data->notification.message != nullptr)
        textmesh::destroyAndUnregisterTextMesh(_data->notification.message);
    textmesh::destroyAndUnregisterTextMesh(_data->uiMaterializeItem);

    if (globalState::playerGUID == getGUID() ||
        globalState::playerPositionRef == &_data->cpd->currentCOMPosition)
    {
        globalState::playerGUID = "";
        globalState::playerPositionRef = nullptr;
    }

    delete _data->characterRenderObj->animator;
    _data->rom->unregisterRenderObjects({ _data->characterRenderObj, _data->handleRenderObj, _data->weaponRenderObj, _data->chirpySphereRenderObj });
    _data->rom->removeModelCallbacks(this);

    physengine::destroyCapsule(_data->cpd);

    delete _data;
}

void updateWazaTimescale(float_t simDeltaTime, SimulationCharacter_XData* d)
{
    d->wazaHitTimescale = physutil::lerp(d->wazaHitTimescale, 1.0f, simDeltaTime * d->wazaHitTimescale * d->wazaHitTimescaleReturnToOneSpeed);
    if (d->wazaHitTimescale > 0.999f)
        d->wazaHitTimescale = 1.0f;
    globalState::timescale = d->wazaHitTimescale;
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

void getRawXZInput(vec2& outInput)
{
    outInput[0] = input::simInputSet().flatPlaneMovement.axisX;
    outInput[1] = input::simInputSet().flatPlaneMovement.axisY;
}

void getCameraOrientedInput(vec3 cameraFacingDirection, vec3& outFlatInput, float_t& outFacingDirection)
{
    vec2 input;
    getRawXZInput(input);
    if (glm_vec2_norm2(input) > 0.000001f)
    {
        // Transform input to world space.
        vec3 flatCameraFacingDirection = {
            cameraFacingDirection[0],
            0.0f,
            cameraFacingDirection[2],
        };
        glm_normalize(flatCameraFacingDirection);

        glm_vec3_scale(flatCameraFacingDirection, input[1], outFlatInput);
        vec3 flatCamRight;
        glm_vec3_crossn(flatCameraFacingDirection, vec3{ 0.0f, 1.0f, 0.0f }, flatCamRight);
        glm_vec3_muladds(flatCamRight, input[0], outFlatInput);
        if (glm_vec3_norm2(outFlatInput) > 1.0f)
            glm_vec3_normalize(outFlatInput);

        outFacingDirection = atan2f(outFlatInput[0], outFlatInput[2]);
    }
    else
        glm_vec3_zero(outFlatInput);
}

void projectAndScale(vec3 delta, vec3 planeNormal, vec3& outDelta)
{
    float_t deltaMag = glm_vec3_norm(delta);

    float_t sqrMag = glm_vec3_norm2(planeNormal);
    if (sqrMag < GLM_FLT_EPSILON)
    {
        glm_vec3_copy(delta, outDelta);
    }
    else
    {
        float_t dot = glm_vec3_dot(delta, planeNormal);
        outDelta[0] = delta[0] - planeNormal[0] * dot / sqrMag;
        outDelta[1] = delta[1] - planeNormal[1] * dot / sqrMag;
        outDelta[2] = delta[2] - planeNormal[2] * dot / sqrMag;
    }

    glm_vec3_scale_as(outDelta, deltaMag, outDelta);
}

constexpr size_t NUM_ITERATIONS = 5;
constexpr float_t SKIN_WIDTH = 0.015f;

void moveFromXZInput(vec3& inoutPosition, vec3 paramDeltaPosition, float_t capsuleRadius, float_t capsuleHeight, JPH::BodyID ignoreBodyId, float_t cosMaxSlopeAngle)
{
    ZoneScoped;

    vec3 deltaPosition;
    glm_vec3_copy(paramDeltaPosition, deltaPosition);

    vec3 initReverseFlatDeltaPositionN;
    glm_vec3_normalize_to(vec3{ -deltaPosition[0], 0.0f, -deltaPosition[2] }, initReverseFlatDeltaPositionN);

    for (size_t i = 0; i < NUM_ITERATIONS; i++)
    {
        float_t castDist = glm_vec3_norm(deltaPosition) + SKIN_WIDTH;

        vec3 currentDeltaN;
        glm_vec3_normalize_to(deltaPosition, currentDeltaN);

        vec3 dirAndMag;
        glm_vec3_scale(currentDeltaN, castDist, dirAndMag);

        float_t hitFrac;
        vec3 hitNormal;
        vec3 contactPosition;
        if (physengine::cylinderCast(inoutPosition, capsuleRadius - SKIN_WIDTH, capsuleHeight, ignoreBodyId, dirAndMag, hitFrac, hitNormal, contactPosition))
        {
            float_t snapDist = castDist * hitFrac - SKIN_WIDTH;
            vec3 snapDelta;
            glm_vec3_scale(currentDeltaN, snapDist, snapDelta);

            {
                // @DEBUG hit visualization.
                vec3 p1, p2;
                glm_vec3_add(inoutPosition, snapDelta, p1);
                glm_vec3_copy(p1, p2);
                glm_vec3_muladds(hitNormal, 1.0f, p2);
                physengine::drawDebugVisLine(p1, p2, physengine::DebugVisLineType::VELOCITY);
            }

            // Subtract deltaPosition with raw snapDelta.
            glm_vec3_sub(deltaPosition, snapDelta, deltaPosition);

            // Prevent movement that would make shape casts unreliable.
            if (snapDist <= SKIN_WIDTH)
                glm_vec3_zero(snapDelta);

            // Check for steps/stairs.
            float_t slopeAngle = glm_vec3_dot(vec3{ 0.0f, 1.0f, 0.0f }, hitNormal);
            constexpr float_t STAIR_CLIMB_HEIGHT_MAX = 0.6f;  //1.0f;  // @HARDCODE
            if (slopeAngle <= cosMaxSlopeAngle &&  // @NOTE: this prevents stair climbing from happening in the stead of slope climbing.
                true)//contactPosition[1] + capsuleHeight * 0.5f + capsuleRadius <= STAIR_CLIMB_HEIGHT_MAX)  @NOTE: this was to check whether it is impossible to climb the stairs or not. However, we should just try the cylinder cast is!
            {
                vec3 anticipatedPosition;
                glm_vec3_add(inoutPosition, snapDelta, anticipatedPosition);
                
                vec3 xzDirectionToTopOfStep;
                glm_vec3_copy(deltaPosition, xzDirectionToTopOfStep);
                xzDirectionToTopOfStep[1] = 0.0f;

                float_t xzDistance = glm_vec3_norm(xzDirectionToTopOfStep);
                if (xzDistance > SKIN_WIDTH)
                {
                    // Cast in direction to get to top of step.
                    vec3 stairQueryPos;
                    glm_vec3_add(anticipatedPosition, vec3{ 0.0f, STAIR_CLIMB_HEIGHT_MAX, 0.0f }, stairQueryPos);

                    float_t stairHitFrac;
                    vec3 stairHitNormal;
                    if (physengine::cylinderCast(stairQueryPos, capsuleRadius - SKIN_WIDTH, capsuleHeight, ignoreBodyId, xzDirectionToTopOfStep, stairHitFrac, stairHitNormal))
                    {
                        if (glm_vec3_dot(vec3{ 0.0f, 1.0f, 0.0f }, stairHitNormal) > cosMaxSlopeAngle)
                        {
                            // Flat floor. Include this move!
                            float_t supplementarySnapDist = xzDistance * stairHitFrac - SKIN_WIDTH;

                            vec3 supplementarySnapDelta;
                            glm_vec3_scale_as(xzDirectionToTopOfStep, supplementarySnapDist, supplementarySnapDelta);
                            supplementarySnapDelta[1] += STAIR_CLIMB_HEIGHT_MAX;

                            glm_vec3_sub(xzDirectionToTopOfStep, supplementarySnapDelta, deltaPosition);

                            if (supplementarySnapDist < 0.0f)
                                glm_vec3_zero(supplementarySnapDelta);  // Prevent char from moving backwards.

                            glm_vec3_add(snapDelta, supplementarySnapDelta, snapDelta);
                        }
                        // With steep wall case, do nothing. Don't include this move.
                    }
                    else
                    {
                        // It looks like it came this far, but we need to make sure there's solid ground beneath us and stick to it!
                        float_t supplementarySnapDist1 = xzDistance - SKIN_WIDTH;

                        vec3 supplementarySnapDelta1;
                        glm_vec3_scale_as(xzDirectionToTopOfStep, supplementarySnapDist1, supplementarySnapDelta1);
                        supplementarySnapDelta1[1] += STAIR_CLIMB_HEIGHT_MAX;

                        if (supplementarySnapDist1 > 0.0f)
                        {
                            // Continue with a downwards cast!
                            vec3 downwardQueryPos;
                            glm_vec3_add(anticipatedPosition, supplementarySnapDelta1, downwardQueryPos);

                            float_t downwardCastDist = STAIR_CLIMB_HEIGHT_MAX + SKIN_WIDTH;

                            if (physengine::cylinderCast(downwardQueryPos, capsuleRadius - SKIN_WIDTH, capsuleHeight, ignoreBodyId, vec3{ 0.0f, -downwardCastDist, 0.0f }, stairHitFrac, stairHitNormal))
                            {
                                int ian = 32;
                                if (glm_vec3_dot(vec3{ 0.0f, 1.0f, 0.0f }, stairHitNormal) > cosMaxSlopeAngle)
                                {
                                    // Success. Flat floor. Include the move!
                                    float_t supplementarySnapDist2 = downwardCastDist * stairHitFrac - SKIN_WIDTH;
                                    vec3 supplementarySnapDelta2 = { 0.0f, -supplementarySnapDist2, 0.0f };

                                    glm_vec3_sub(xzDirectionToTopOfStep, supplementarySnapDelta1, deltaPosition);

                                    glm_vec3_addadd(supplementarySnapDelta1, supplementarySnapDelta2, snapDelta);
                                }
                            }
                        }
                    }
                }
            }

            // Adjust deltaPosition.
            if (slopeAngle > cosMaxSlopeAngle)
            {
                // Adjust the hit normal so char will climb straight up slopes.
                // @NOTE: Adapted from line-plane intersection algorithm.
                // @REF: https://stackoverflow.com/questions/5666222/3d-line-plane-intersection
                float_t scale = glm_vec3_norm(deltaPosition);
                deltaPosition[1] = -glm_vec3_dot(hitNormal, vec3{ deltaPosition[0], 0.0f, deltaPosition[2] }) / hitNormal[1];
                glm_vec3_scale_as(deltaPosition, scale, deltaPosition);
            }
            else
            {
                // Steep wall.
                vec3 flatHitNormalN;
                glm_vec3_normalize_to(vec3{ hitNormal[0], 0.0f, hitNormal[2] }, flatHitNormalN);

                float_t scale = 1.0f - glm_vec3_dot(flatHitNormalN, initReverseFlatDeltaPositionN);

                projectAndScale(vec3{ deltaPosition[0], 0.0f, deltaPosition[2] }, flatHitNormalN, deltaPosition);

                vec3 deltaPositionN;
                glm_vec3_normalize_to(deltaPosition, deltaPositionN);
                if (glm_vec3_dot(deltaPositionN, initReverseFlatDeltaPositionN) > 0.0f)  // >0 bc reversed init flat delta.
                    glm_vec3_zero(deltaPosition);  // @NOTE: Remove further iterations of delta position so that if collide and slide algorithm thinks going against the input direction is okay, this prevents it.  -Timo 2024/02/09
                else if (glm_vec3_norm2(deltaPosition) > scale * scale)
                    glm_vec3_scale(deltaPositionN, scale, deltaPosition);  // @NOTE: this is for limiting the distance of the slide when running up against walls.  -Timo 2024/02/09
            }

            // Move as far as possible.
            glm_vec3_add(inoutPosition, snapDelta, inoutPosition);
        }
        else
        {
            // Free to continue.
            glm_vec3_add(inoutPosition, deltaPosition, inoutPosition);
            break;
        }
    }
}

void moveFromYInput(vec3& inoutPosition, vec3 paramDeltaPosition, float_t capsuleRadius, float_t capsuleHeight, JPH::BodyID ignoreBodyId, float_t cosMaxGroundSlopeAngle, float_t cosMaxCeilingSlopeAngle, bool& outGrounded, bool& outHitCeiling)
{
    ZoneScoped;

    vec3 deltaPosition;
    glm_vec3_copy(paramDeltaPosition, deltaPosition);

    outGrounded = false;
    outHitCeiling = false;

    for (size_t i = 0; i < NUM_ITERATIONS; i++)
    {
        float_t castDist = glm_vec3_norm(deltaPosition) + SKIN_WIDTH;

        vec3 currentDeltaN;
        glm_vec3_normalize_to(deltaPosition, currentDeltaN);

        vec3 dirAndMag;
        glm_vec3_scale(currentDeltaN, castDist, dirAndMag);

        float_t hitFrac;
        vec3 hitNormal;
        if (physengine::cylinderCast(inoutPosition, capsuleRadius - SKIN_WIDTH, capsuleHeight, ignoreBodyId, dirAndMag, hitFrac, hitNormal))
        {
            float_t snapDist = castDist * hitFrac - SKIN_WIDTH;
            vec3 snapDelta;
            glm_vec3_scale(currentDeltaN, snapDist, snapDelta);

            {
                // @DEBUG visualization.
                vec3 p1, p2;
                glm_vec3_add(inoutPosition, snapDelta, p1);
                glm_vec3_copy(p1, p2);
                glm_vec3_muladds(hitNormal, 1.0f, p2);
                physengine::drawDebugVisLine(p1, p2, physengine::DebugVisLineType::KIKKOARMY);
            }

            // Subtract deltaPosition with raw snapDelta.
            glm_vec3_sub(deltaPosition, snapDelta, deltaPosition);

            if (snapDist <= SKIN_WIDTH)
                glm_vec3_zero(snapDelta);

            // Adjust deltaPosition.
            float_t upDNormal = glm_vec3_dot(vec3{ 0.0f, 1.0f, 0.0f }, hitNormal);
            if (upDNormal > cosMaxGroundSlopeAngle)
            {
                // Flat ground.
                glm_vec3_add(inoutPosition, snapDelta, inoutPosition);
                outGrounded = true;
                break;
            }
            else if (-upDNormal > cosMaxCeilingSlopeAngle)
            {
                // Flat ceiling.
                glm_vec3_add(inoutPosition, snapDelta, inoutPosition);
                outHitCeiling = true;
                break;
            }
            else
            {
                // Steep wall.
                glm_vec3_add(inoutPosition, snapDelta, inoutPosition);
                projectAndScale(deltaPosition, hitNormal, deltaPosition);
            }
        }
        else
        {
            // Free to continue.
            glm_vec3_add(inoutPosition, deltaPosition, inoutPosition);
            break;
        }
    }
}

bool moveToTryStickToGround(vec3& inoutPosition, vec3 paramDeltaPosition, float_t capsuleRadius, float_t capsuleHeight, JPH::BodyID ignoreBodyId, float_t cosMaxSlopeAngle)
{
    ZoneScoped;

    vec3 deltaPosition;
    glm_vec3_copy(paramDeltaPosition, deltaPosition);

    vec3 possibleNewPosition;
    glm_vec3_copy(inoutPosition, possibleNewPosition);

    bool grounded = false;

    for (size_t i = 0; i < NUM_ITERATIONS; i++)
    {
        float_t castDist = glm_vec3_norm(deltaPosition) + SKIN_WIDTH;

        vec3 currentDeltaN;
        glm_vec3_normalize_to(deltaPosition, currentDeltaN);

        vec3 dirAndMag;
        glm_vec3_scale(currentDeltaN, castDist, dirAndMag);

        float_t hitFrac;
        vec3 hitNormal;
        if (physengine::cylinderCast(possibleNewPosition, capsuleRadius - SKIN_WIDTH, capsuleHeight, ignoreBodyId, dirAndMag, hitFrac, hitNormal))
        {
            float_t snapDist = castDist * hitFrac - SKIN_WIDTH;
            vec3 snapDelta;
            glm_vec3_scale(currentDeltaN, snapDist, snapDelta);

            {
                // @DEBUG visualization.
                vec3 p1, p2;
                glm_vec3_add(possibleNewPosition, snapDelta, p1);
                glm_vec3_copy(p1, p2);
                glm_vec3_muladds(hitNormal, 1.0f, p2);
                physengine::drawDebugVisLine(p1, p2, physengine::DebugVisLineType::KIKKOARMY);
            }

            // Subtract deltaPosition with raw snapDelta.
            glm_vec3_sub(deltaPosition, snapDelta, deltaPosition);

            if (snapDist <= SKIN_WIDTH)
                glm_vec3_zero(snapDelta);

            // Adjust deltaPosition.
            if (glm_vec3_dot(vec3{ 0.0f, 1.0f, 0.0f }, hitNormal) > cosMaxSlopeAngle)
            {
                // Flat ground. Confirmed ground to stick to.
                glm_vec3_add(possibleNewPosition, snapDelta, possibleNewPosition);
                grounded = true;
                break;
            }
            else
            {
                // Steep wall. Possibly there is ground to stick to, so continue on!
                glm_vec3_add(possibleNewPosition, snapDelta, possibleNewPosition);
                projectAndScale(deltaPosition, hitNormal, deltaPosition);
            }
        }
        else
        {
            // No collision. Confirmed no ground to stick to.
            break;
        }
    }

    if (grounded)
        glm_vec3_copy(possibleNewPosition, inoutPosition);

    return grounded;
}

void processCollideAndSlideBackend(SimulationCharacter_XData::BackendMovementInputState& bmis, physengine::CapsulePhysicsData* cpd, float_t simDeltaTime)
{
    ZoneScoped;

    // Use collide and slide algorithm.
    vec3 currentPosition;
    physengine::getCharacterPosition(*cpd, currentPosition);

    vec3 prevPosition;
    glm_vec3_copy(currentPosition, prevPosition);

    if (glm_vec3_norm2(bmis.inputVelocity) > 0.000001f)
    {
        vec3 deltaPosition;
        glm_vec3_scale(bmis.inputVelocity, simDeltaTime, deltaPosition);
        moveFromXZInput(currentPosition, deltaPosition, cpd->radius, cpd->height, cpd->character->GetBodyID(), bmis.cosMaxGroundSlopeAngle);
    }

    bool grounded, hitCeiling;
    float_t gravityDelta;
    {
        vec3 deltaPosition;
        glm_vec3_scale(bmis.gravityDirection, bmis.verticalVelocity * simDeltaTime, deltaPosition);
        gravityDelta = (bmis.verticalVelocity > 0.0f ? glm_vec3_norm(deltaPosition) : 0.0f);  // Don't include sticking to ground when moving upward.
        moveFromYInput(currentPosition, deltaPosition, cpd->radius, cpd->height, cpd->character->GetBodyID(), bmis.cosMaxGroundSlopeAngle, bmis.cosMaxCeilingSlopeAngle, grounded, hitCeiling);
    }

    if (!grounded && bmis.attemptStickToGround && bmis.stickToGroundMaxDelta > gravityDelta)
    {
        // Check remaining room to see if there's flat ground beneath.
        vec3 deltaPosition;
        physengine::getWorldGravity(deltaPosition);
        glm_vec3_normalize(deltaPosition);
        glm_vec3_scale(deltaPosition, bmis.stickToGroundMaxDelta - gravityDelta, deltaPosition);
        grounded = moveToTryStickToGround(currentPosition, deltaPosition, cpd->radius, cpd->height, cpd->character->GetBodyID(), bmis.cosMaxGroundSlopeAngle);
    }

    // Move.
    vec3 velocity;
    glm_vec3_sub(currentPosition, prevPosition, velocity);
    glm_vec3_scale(velocity, 1.0f / simDeltaTime, velocity);
    physengine::moveCharacter(*cpd, velocity);

    // Set state & outputs.
    glm_vec3_copy(velocity, bmis.inputVelocity);
    bmis.inputVelocity[1] = 0.0f;
    if (glm_vec3_norm2(bmis.inputVelocity) < 0.000001f)
        glm_vec3_zero(bmis.inputVelocity);

    if (grounded || hitCeiling)
        bmis.verticalVelocity = -bmis.gravityMagnitude * simDeltaTime;
    else
        bmis.verticalVelocity -= bmis.gravityMagnitude * simDeltaTime;
    bmis.attemptStickToGround = grounded;

    bmis.isGrounded = grounded;
    bmis.hitCeiling = hitCeiling;
}

inline void frontendMovementStanding(SimulationCharacter_XData* d)
{
    // Falling.
    if (!d->bmis.isGrounded)
    {
        d->fmis.setMvtType(
            SimulationCharacter_XData::FrontendMovementInputState::MovementType::FALLING
        );
        return;
    }

    // Jumping.
    if (input::simInputSet().jump.onAction)
    {
        d->fmis.setMvtType(
            SimulationCharacter_XData::FrontendMovementInputState::MovementType::JUMPING
        );
        return;
    }

    // XZ Movement.
    vec2 rawInput;
    getRawXZInput(rawInput);
    if (glm_vec2_norm2(rawInput) > 0.000001f)
    {
        d->fmis.setMvtType(
            SimulationCharacter_XData::FrontendMovementInputState::MovementType::RUNNING
        );
        return;
    }

    // Write out XZ movement.
    glm_vec3_zero(d->bmis.inputVelocity);
}

inline void frontendMovementRunning(SimulationCharacter_XData* d, float_t simDeltaTime)
{
    // Falling.
    if (!d->bmis.isGrounded)
    {
        d->fmis.setMvtType(
            SimulationCharacter_XData::FrontendMovementInputState::MovementType::FALLING
        );
        return;
    }

    // Jumping.
    if (input::simInputSet().jump.onAction)
    {
        d->fmis.setMvtType(
            SimulationCharacter_XData::FrontendMovementInputState::MovementType::JUMPING
        );
        return;
    }

    // XZ Movement.
    vec3 rawInput;
    float_t rawFacingDirection;
    getCameraOrientedInput(
        d->camera->sceneCamera.facingDirection,
        rawInput,
        rawFacingDirection
    );
    bool isMoving = (glm_vec3_norm2(rawInput) > 0.000001f);

    // Turn around if input direction is opposite of current moving direction.
    if (d->fmis.mvtTypeSteps == 0)
    {
        if (glm_vec3_norm2(d->bmis.inputVelocity) > 0.000001f)
        {
            // Inherit velocity direction of landing.
            d->facingDirection = std::atan2f(d->bmis.inputVelocity[0], d->bmis.inputVelocity[2]);
            d->fmis.currentSpeed = glm_vec3_norm(d->bmis.inputVelocity);
        }
        else
        {
            // Face input direction first frame of start-run.
            if (isMoving)
                d->facingDirection = rawFacingDirection;
            d->fmis.currentSpeed = 0.0f;
        }
    }
    else
    {
        if (isMoving)
        {
            vec3 inputVeloN;
            glm_vec3_normalize_to(d->bmis.inputVelocity, inputVeloN);
            vec3 rawInputN;
            glm_vec3_normalize_to(rawInput, rawInputN);
            if (glm_vec3_dot(inputVeloN, rawInputN) < -std::cosf(glm_rad(30.0f)))  // @HARDCODE
            {
                d->facingDirection = rawFacingDirection;
                d->fmis.currentSpeed *= -1.0f;
            }

            // Turn towards input direction with max turning angle.
            d->facingDirection =
                physutil::moveTowardsAngle(d->facingDirection, rawFacingDirection, d->fmis.turningSpeed * simDeltaTime);
        }
    }

    // Accelerate/decelerate.
    float_t targetSpeed = glm_vec3_norm(rawInput) * d->fmis.maxSpeed;
    size_t accelOrDecel = (targetSpeed > d->fmis.currentSpeed ? 0 : 1);
    float_t maxDeltaSpeed =
        (d->fmis.currentSpeed < 0.0f ?
            d->fmis.negGroundedAccelDecel[accelOrDecel] :
            d->fmis.groundedAccelDecel[accelOrDecel])
            * simDeltaTime;
    d->fmis.currentSpeed =
        physutil::moveTowards(d->fmis.currentSpeed, targetSpeed, maxDeltaSpeed);

    // Standing.
    if (!isMoving && std::abs(d->fmis.currentSpeed) < 0.000001f)
    {
        d->fmis.setMvtType(
            SimulationCharacter_XData::FrontendMovementInputState::MovementType::STANDING
        );
        return;
    }

    // Write backend input.
    vec3 eulerAngles = { 0.0f, d->facingDirection, 0.0f };
    mat4 rotation;
    glm_euler_zyx(eulerAngles, rotation);
    mat3 rotation3;
    glm_mat4_pick3(rotation, rotation3);
    vec3 moveDirection;
    glm_mat3_mulv(rotation3, vec3{ 0.0f, 0.0f, 1.0f }, moveDirection);
    glm_vec3_scale(moveDirection, d->fmis.currentSpeed, d->bmis.inputVelocity);
}

inline void frontendMovementFallingAndJumping(SimulationCharacter_XData* d, float_t simDeltaTime)
{
    // Landed.
    if (d->fmis.mvtTypeSteps > 0 && d->bmis.isGrounded)
    {
        vec2 rawInput;
        getRawXZInput(rawInput);
        d->fmis.setMvtType(
            SimulationCharacter_XData::FrontendMovementInputState::MovementType::RUNNING
        );
        return;
    }

    // Jumping Movement.
    if (d->fmis.mvtTypeSteps == 0 &&
        d->fmis.currentMvtType == SimulationCharacter_XData::FrontendMovementInputState::MovementType::JUMPING)
    {
        d->bmis.verticalVelocity = 14.0f;  // @HARDCODE.
    }

    // XZ Movement.
    vec3 rawInput;
    float_t rawFacingDirection;
    getCameraOrientedInput(
        d->camera->sceneCamera.facingDirection,
        rawInput,
        rawFacingDirection
    );
    bool isMoving = (glm_vec3_norm2(rawInput) > 0.000001f);

    if (d->fmis.mvtTypeSteps == 0 && isMoving)
        d->facingDirection = rawFacingDirection;  // Face input direction first tick of jump/fall.

    // Find target input.
    vec3 targetInput;
    glm_vec3_scale(rawInput, d->fmis.maxSpeed, targetInput);

    // Find max delta speed.
    size_t accelOrDecel =
        (glm_vec3_norm2(targetInput) > d->fmis.currentSpeed * d->fmis.currentSpeed ? 0 : 1);
    float_t maxDeltaSpeed =
        d->fmis.airborneAccelDecel[accelOrDecel] * simDeltaTime;

    // Move towards target input.
    vec3 deltaToTargetInput;
    glm_vec3_sub(targetInput, d->bmis.inputVelocity, deltaToTargetInput);
    if (glm_vec3_norm2(deltaToTargetInput) > maxDeltaSpeed * maxDeltaSpeed)
        glm_vec3_scale_as(deltaToTargetInput, maxDeltaSpeed, deltaToTargetInput);
    glm_vec3_add(d->bmis.inputVelocity, deltaToTargetInput, d->bmis.inputVelocity);
}

void EXPERIMENTAL__playerCombatStateMachine(SimulationCharacter_XData* d, int32_t timing)
{
    bool timingInAcceptableRange =
        (timing >= d->csm.inputAcceptableRange[0] && timing <= d->csm.inputAcceptableRange[1]);
    bool timingBeginningOfInputRange = (timing == d->csm.inputAcceptableRange[0]);
    bool timingEndOfInputRange = (timing == d->csm.inputAcceptableRange[1]);

    // Allow input for this new beat.
    if (timingBeginningOfInputRange)
    {
        d->csm.playerInputtedActionThisBeat = false;
    }

    // Process state machine.
    switch (d->csm.playerCombatState)
    {
        typedef SimulationCharacter_XData::EXPERIMENTAL__ShouldbeInSeparateClassCombatStateMachine::CombatState CombatState_e;

        case CombatState_e::IDLE:
        {
            // Press attack input for charging weapon.
            if (timingInAcceptableRange && input::simInputSet().attack.onAction)
            {
                d->csm.playerNextCombatState = CombatState_e::WEAPON_CHARGING;
                d->csm.playerInputtedActionThisBeat = true;
            }

            // Pretend like attack input press didn't happen.
            if (d->csm.playerNextCombatState == CombatState_e::WEAPON_CHARGING &&
                input::simInputSet().attack.onRelease)
            {
                d->csm.playerNextCombatState = CombatState_e::IDLE;
                d->csm.playerInputtedActionThisBeat = false;
            }
        } break;

        case CombatState_e::WEAPON_CHARGING:
        {
            // Release charge (can't go back into charging state until next beat).
            if (input::simInputSet().attack.onRelease &&
                !d->csm.playerInputtedActionThisBeat)
            {
                // For real attack.
                if (timingInAcceptableRange)
                {
                    d->csm.playerNextCombatState = CombatState_e::ATTACK;
                    d->csm.playerInputtedActionThisBeat = true;
                }

                // For missing attack beat and returning to Idle.
                else
                {
                    d->csm.playerNextCombatState = CombatState_e::IDLE;
                    d->csm.playerInputtedActionThisBeat = true;
                }
            }
        } break;

        case CombatState_e::ATTACK:
        {
            // No input accepted for this round. Don't wait for player.
            d->csm.playerNextCombatState = CombatState_e::IDLE;
            d->csm.playerInputtedActionThisBeat = true;
        } break;
    }
}

void EXPERIMENTAL__enemyCombatStateMachine(SimulationCharacter_XData* d, int32_t timing)
{
    if (timing != 0)
        return;  // Only process if on downbeat.

    // Process state machine.
    switch (d->csm.enemyCombatState)
    {
        typedef SimulationCharacter_XData::EXPERIMENTAL__ShouldbeInSeparateClassCombatStateMachine::CombatState CombatState_e;

        case CombatState_e::IDLE:
        {
            d->csm.enemyNextCombatState = CombatState_e::WEAPON_CHARGING;
        } break;

        case CombatState_e::WEAPON_CHARGING:
        {
            d->csm.enemyNextCombatState = CombatState_e::ATTACK;
        } break;

        case CombatState_e::ATTACK:
        {
            d->csm.enemyNextCombatState = CombatState_e::IDLE;
        } break;
    }
}

void EXPERIMENTAL__combatInteraction(SimulationCharacter_XData* d, int32_t timing)
{
    // Lock interaction criteria.
    if (timing < 0)
        d->csm.interactionProcessed = false;

    if (d->csm.interactionProcessed)
        return;

    // Detect if everything is determined.
    typedef SimulationCharacter_XData::EXPERIMENTAL__ShouldbeInSeparateClassCombatStateMachine::CombatState CombatState_e;
    bool readyToCommitBeat =
        timing >= 0 &&
        (d->csm.playerCombatState != CombatState_e::WEAPON_CHARGING ||
            d->csm.playerInputtedActionThisBeat ||
            timing == d->csm.inputAcceptableRange[1]);
    if (!readyToCommitBeat)
        return;

    // Increment player and enemy.
    d->csm.playerCombatState = d->csm.playerNextCombatState;
    d->csm.enemyCombatState = d->csm.enemyNextCombatState;

    // Attack each other!
    if (d->csm.playerCombatState == CombatState_e::ATTACK &&
        d->csm.enemyCombatState == CombatState_e::ATTACK)
    {
        // Block each other.
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_hollow_knight_sfx/hero_parry.wav",
        });
    }
    else if (d->csm.playerCombatState == CombatState_e::ATTACK)
    {
        // Hit landed on enemy.
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_hollow_knight_sfx/hero_nail_art_great_slash.wav",
        });
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_EnemyHit_Critical.wav",
        });
    }
    else if (d->csm.enemyCombatState == CombatState_e::ATTACK)
    {
        // Hit landed on player.
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_hollow_knight_sfx/hero_butterfly_blade.wav",
        });
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_bonk.ogg",
        });
    }

    // Charge player weapon.
    if (d->csm.playerCombatState == CombatState_e::WEAPON_CHARGING)
    {
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_hollow_knight_sfx/hero_super_dash_ready.wav",
        });
    }

    // Charge enemy weapon.
    if (d->csm.enemyCombatState == CombatState_e::WEAPON_CHARGING)
    {
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_hollow_knight_sfx/hero_dash.wav",
        });
    }

    // Finish.
    d->csm.interactionProcessed = true;
}

void EXPERIMENTAL__TickCombatStateMachine(SimulationCharacter_XData* d)
{
    int32_t timing = d->csm.currentBeat % d->csm.tempo;
    if (timing > d->csm.tempo / 2)
        timing -= d->csm.tempo;

    // Process input state machines.
    EXPERIMENTAL__playerCombatStateMachine(d, timing);
    EXPERIMENTAL__enemyCombatStateMachine(d, timing);

    // Process interactions.
    EXPERIMENTAL__combatInteraction(d, timing);

    // Heartbeat sound cue.
    if (timing == 0)
        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_heart_down_0.wav",
            "res/sfx/wip_heart_down_1.wav",
            "res/sfx/wip_heart_down_2.wav",
            "res/sfx/wip_heart_down_3.wav",
            "res/sfx/wip_heart_down_4.wav",
        });
        // AudioEngine::getInstance().playSoundFromList({
        //     "res/sfx/wip_heart_up_0.wav",
        //     "res/sfx/wip_heart_up_1.wav",
        //     "res/sfx/wip_heart_up_2.wav",
        //     "res/sfx/wip_heart_up_3.wav",
        //     "res/sfx/wip_heart_up_4.wav",
        // });

    // End tick.
    d->csm.currentBeat++;
}

void defaultPhysicsUpdate(float_t simDeltaTime, SimulationCharacter_XData* d, EntityManager* em, const std::string& myGuid)
{
    ZoneScoped;

    // @NOCHECKIN: @EXPERIMENT /////////////////////////////////////////////////////////////////////////////////////////
    if (isPlayer(d))
    {
        EXPERIMENTAL__TickCombatStateMachine(d);

#if 0
        static int32_t jojo = 0;
        // static int32_t tempo = 30;
        static int32_t tempo = 20;

        static ivec2 acceptableRange = { -5, 10 };  // @NOTE: based off this data: https://www.desmos.com/calculator/gttn6iwzy6

        static std::vector<int32_t> onActionInputs;
        static std::vector<int32_t> onReleaseInputs;

        static bool isHoldingAChargedAttack = false;
        static int32_t startedChargedAttackHoldOnBeat = 0;

        int32_t timing = jojo % tempo;
        if (timing > tempo / 2)
            timing -= tempo;  // Wrap to negative if too high.

        if (input::simInputSet().attack.onAction)
        {
            if (timing >= acceptableRange[0] && timing <= acceptableRange[1])
            {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_hollow_knight_sfx/hero_super_dash_ready.wav",
                });

                isHoldingAChargedAttack = true;
                startedChargedAttackHoldOnBeat = jojo;
            }
            onActionInputs.push_back(timing);
            std::cout << timing << std::endl;
        }
        else if (input::simInputSet().attack.onRelease)
        {
            if (timing >= acceptableRange[0] && timing <= acceptableRange[1])
            {
                if (isHoldingAChargedAttack && startedChargedAttackHoldOnBeat < jojo)
                {
                    AudioEngine::getInstance().playSoundFromList({
                        "res/sfx/wip_hollow_knight_sfx/hero_nail_art_great_slash.wav",
                    });
                }
            }

            isHoldingAChargedAttack = false;
            startedChargedAttackHoldOnBeat = 0;

            onReleaseInputs.push_back(timing);
            std::cout << timing << std::endl;
        }

        #define _ENEMY_SECTION_IF_YOU_CAN_IMAGINE_
        #ifdef _ENEMY_SECTION_IF_YOU_CAN_IMAGINE_
        {
            static int32_t beatMeasure = 4;  // Enemy's rhythm is 4 beats per measure.
            static int32_t currentBeat = 0;
            if (timing == 0)
            {
                // Downbeat.
                switch (currentBeat)
                {
                    case 0:
                        break;

                    case 1:
                        break;

                    case 2:
                        AudioEngine::getInstance().playSoundFromList({
                            "res/sfx/wip_hollow_knight_sfx/hero_dash.wav",
                        });
                        break;

                    case 3:
                        AudioEngine::getInstance().playSoundFromList({
                            "res/sfx/wip_hollow_knight_sfx/hero_butterfly_blade.wav",
                        });
                        break;
                }

                // Increment.
                currentBeat = (currentBeat + 1) % beatMeasure;
            }
        }
        #endif  // _ENEMY_SECTION_IF_YOU_CAN_IMAGINE_

        // Heartbeat sound cue.
        if (timing == 0)
            AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_heart_down_0.wav",
                "res/sfx/wip_heart_down_1.wav",
                "res/sfx/wip_heart_down_2.wav",
                "res/sfx/wip_heart_down_3.wav",
                "res/sfx/wip_heart_down_4.wav",
            });
            // AudioEngine::getInstance().playSoundFromList({
            //     "res/sfx/wip_heart_up_0.wav",
            //     "res/sfx/wip_heart_up_1.wav",
            //     "res/sfx/wip_heart_up_2.wav",
            //     "res/sfx/wip_heart_up_3.wav",
            //     "res/sfx/wip_heart_up_4.wav",
            // });

        // ZA ENDO!!!!
        jojo++;

        // Create input latency report.
        // @THOUGHT: I think this is gonna have to be needed for every run thru. Some kind of calibration both audibly and visually.  -Timo 2024/02/26
        if (input::simInputSet().detach.onAction)
        {
            std::cout << "ON ACTION:" << std::endl;
            std::map<int32_t, int32_t> onActionMap;
            for (auto input : onActionInputs)
                onActionMap[input]++;
            for (auto it = onActionMap.begin(); it != onActionMap.end(); it++)
                std::cout << it->first << ": " << it->second << std::endl;

            std::cout << std::endl;

            std::cout << "ON RELEASE:" << std::endl;
            std::map<int32_t, int32_t> onReleaseMap;
            for (auto input : onReleaseInputs)
                onReleaseMap[input]++;
            for (auto it = onReleaseMap.begin(); it != onReleaseMap.end(); it++)
                std::cout << it->first << ": " << it->second << std::endl;
        }

#endif

    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Frontend movement state machine.
    if (isPlayer(d) && !d->disableInput)  // @INCOMPLETE: should have `disableInput` be connected to the actual inputs of this, not part of this if statement.
    {
        ZoneScopedN("Frontend mvt state machine");

        SimulationCharacter_XData::FrontendMovementInputState::MovementType mvtTypeCopy;
        do
        {
            mvtTypeCopy = d->fmis.currentMvtType;
            switch (d->fmis.currentMvtType)
            {
                case SimulationCharacter_XData::FrontendMovementInputState::MovementType::STANDING:
                    frontendMovementStanding(d);
                    break;
                case SimulationCharacter_XData::FrontendMovementInputState::MovementType::RUNNING:
                    frontendMovementRunning(d, simDeltaTime);
                    break;
                case SimulationCharacter_XData::FrontendMovementInputState::MovementType::FALLING:
                case SimulationCharacter_XData::FrontendMovementInputState::MovementType::JUMPING:
                    frontendMovementFallingAndJumping(d, simDeltaTime);
                    break;
            }
        } while (mvtTypeCopy != d->fmis.currentMvtType);
        d->fmis.mvtTypeSteps++;
    }

    // Update facing direction with cosmetic simulation transform.
    vec3 eulerAngles = { 0.0f, d->facingDirection, 0.0f };
    mat4 rotation;
    glm_euler_zyx(eulerAngles, rotation);
    versor rotationV;
    glm_mat4_quat(rotation, rotationV);
    physengine::updateSimulationTransformRotation(d->cpd->simTransformId, rotationV);

    // Run movement backend.
    processCollideAndSlideBackend(d->bmis, d->cpd, simDeltaTime);
}

void calculateBladeStartEndFromHandAttachment(SimulationCharacter_XData* d, vec3& bladeStart, vec3& bladeEnd)
{
    mat4 offsetMat = GLM_MAT4_IDENTITY_INIT;
    glm_translate(offsetMat, vec3{ 0.0f, -physengine::getLengthOffsetToBase(*d->cpd) / d->modelSize, 0.0f });

    mat4 attachmentJointMat;
    d->characterRenderObj->animator->getJointMatrix(d->attackWazaEditor.bladeBoneName, attachmentJointMat);
    glm_mat4_mul(offsetMat, attachmentJointMat, attachmentJointMat);

    glm_mat4_mulv3(attachmentJointMat, vec3{ 0.0f, d->attackWazaEditor.bladeDistanceStartEnd[0], 0.0f }, 1.0f, bladeStart);
    glm_mat4_mulv3(attachmentJointMat, vec3{ 0.0f, d->attackWazaEditor.bladeDistanceStartEnd[1], 0.0f }, 1.0f, bladeEnd);
}

void attackWazaEditorPhysicsUpdate(float_t simDeltaTime, SimulationCharacter_XData* d)
{
    if (d->attackWazaEditor.triggerRecalcWazaCache)
    {
        SimulationCharacter_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

        d->attackWazaEditor.minTick = 0;
        d->attackWazaEditor.maxTick = aw.duration >= 0 ? aw.duration : 100;  // @HARDCODE: if duration is infinite, just cap it at 100.  -Timo 2023/09/22

        d->characterRenderObj->animator->setState(aw.animationState, d->attackWazaEditor.currentTick * simDeltaTime);

        d->attackWazaEditor.triggerRecalcWazaCache = false;
    }

    if (d->attackWazaEditor.triggerRecalcHitscanLaunchVelocityCache)
    {
        SimulationCharacter_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

        d->attackWazaEditor.hitscanLaunchVelocitySimCache.clear();
        vec3s currentPosition;
        glm_vec3_copy(aw.hitscanLaunchRelPosition, currentPosition.raw);
        vec3  launchVelocityCopy;
        glm_vec3_copy(aw.hitscanLaunchVelocity, launchVelocityCopy);

        float_t knockedbackTimer = d->knockedbackTime;
        SimulationCharacter_XData::KnockbackStage knockbackMode = SimulationCharacter_XData::KnockbackStage::KNOCKED_UP;

        for (size_t i = 0; i < 100; i++)
        {
            vec3 deltaPosition;
            glm_vec3_scale(launchVelocityCopy, simDeltaTime, deltaPosition);
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
                    if (knockbackMode == SimulationCharacter_XData::KnockbackStage::RECOVERY)
                    {
                        maxAllowedDeltaMagnitude = d->recoveryGroundedXZDeceleration;
                    }
                    else if (knockbackMode == SimulationCharacter_XData::KnockbackStage::KNOCKED_UP)
                    {
                        maxAllowedDeltaMagnitude = d->knockedbackGroundedXZDeceleration;
                    }

            if (glm_vec3_norm2(xzVelocityDampen) > maxAllowedDeltaMagnitude * maxAllowedDeltaMagnitude)
                glm_vec3_scale_as(xzVelocityDampen, maxAllowedDeltaMagnitude, xzVelocityDampen);
            glm_vec3_add(launchVelocityCopy, xzVelocityDampen, launchVelocityCopy);

            // @COPYPASTA
            if (knockbackMode == SimulationCharacter_XData::KnockbackStage::KNOCKED_UP)
            {
                if (knockedbackTimer < 0.0f)
                {
                    knockbackMode = SimulationCharacter_XData::KnockbackStage::RECOVERY;
                }
                else
                    knockedbackTimer -= simDeltaTime;
            }
        }

        d->attackWazaEditor.hitscanLaunchAndSelfVelocityAwaseIndex = 0;
        d->attackWazaEditor.triggerRecalcHitscanLaunchVelocityCache = false;
    }

    if (d->attackWazaEditor.triggerRecalcSelfVelocitySimCache)
    {
        SimulationCharacter_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

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
            glm_vec3_scale(currentVelocity, simDeltaTime, deltaPosition);
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
        SimulationCharacter_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex];

        // Fill in hitscan flow nodes according to baked range.
        aw.hitscanNodes.clear();
        for (int16_t i = d->attackWazaEditor.bakeHitscanStartTick; i <= d->attackWazaEditor.bakeHitscanEndTick; i++)
        {
            d->characterRenderObj->animator->setState(aw.animationState, i * simDeltaTime, true);

            SimulationCharacter_XData::AttackWaza::HitscanFlowNode hfn;
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
    std::vector<SimulationCharacter_XData::AttackWaza::HitscanFlowNode>& hnodes = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].hitscanNodes;
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
    SimulationCharacter_XData::AttackWaza::VacuumSuckIn& vsi = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].vacuumSuckIn;
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
    SimulationCharacter_XData::AttackWaza::ForceZone& fz = d->attackWazaEditor.editingWazaSet[d->attackWazaEditor.wazaIndex].forceZone;
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

void SimulationCharacter::simulationUpdate(float_t simDeltaTime)
{
    // @DEBUG: for level editor
    _data->disableInput = (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput);
    
    if (_data->wazaHitTimescale < 1.0f)
        updateWazaTimescale(simDeltaTime, _data);

    if (isPlayer(_data))
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
        _data->iframesTimer -= simDeltaTime;

    // Process physics updates depending on the mode.
    if (_data->attackWazaEditor.isEditingMode)
        attackWazaEditorPhysicsUpdate(simDeltaTime, _data);
    else
        defaultPhysicsUpdate(simDeltaTime, _data, _em, getGUID());
}

void SimulationCharacter::update(float_t deltaTime)
{
    // @DEBUG: for level editor
    _data->disableInput = (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput);

    // Update twitch angle.
    _data->characterRenderObj->animator->setTwitchAngle(_data->attackTwitchAngle);
    _data->attackTwitchAngle = glm_lerp(_data->attackTwitchAngle, 0.0f, std::abs(_data->attackTwitchAngle) * _data->attackTwitchAngleReturnSpeed * 60.0f * deltaTime);
}

void SimulationCharacter::lateUpdate(float_t deltaTime)
{
    if (_data->attackWazaEditor.isEditingMode)
        _data->facingDirection = 0.0f;  // @NOTE: this needs to be facing in the default facing direction so that the hitscan node positions are facing in the default direction when baked.

    //
    // Update position of character and weapon
    //
    if (_data->movingPlatformAttachment.attachmentStage >= SimulationCharacter_XData::MovingPlatformAttachment::AttachmentStage::FIRST_DELTA_ATTACHMENT)
        _data->facingDirection += _data->movingPlatformAttachment.attachmentYAxisAngularVelocity * deltaTime;

    vec3 offset(0.0f, -physengine::getLengthOffsetToBase(*_data->cpd), 0.0f);
    vec3 position;
    glm_vec3_add(_data->cpd->interpolCOMPosition, offset, position);

    vec3 eulerAngles = { 0.0f, _data->facingDirection, 0.0f };
    mat4 rotation;
    glm_euler_zyx(eulerAngles, rotation);

    mat4 transform = GLM_MAT4_IDENTITY_INIT;
    glm_translate(transform, position);
    glm_mat4_mul(transform, rotation, transform);
    glm_scale(transform, vec3{ _data->modelSize, _data->modelSize, _data->modelSize });
    glm_mat4_copy(transform, _data->characterRenderObj->transformMatrix);

    mat4 attachmentJointMat;
    _data->characterRenderObj->animator->getJointMatrix(_data->weaponAttachmentJointName, attachmentJointMat);
    glm_mat4_mul(_data->characterRenderObj->transformMatrix, attachmentJointMat, _data->weaponRenderObj->transformMatrix);
    glm_mat4_copy(_data->weaponRenderObj->transformMatrix, _data->handleRenderObj->transformMatrix);
}

void SimulationCharacter::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpString(_data->characterType);
    if (isPlayer(_data))
    {
        std::cerr << "ERROR: attempting to save player character." << std::endl;
        HAWSOO_CRASH();
    }
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

void SimulationCharacter::load(DataSerialized& ds)
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

bool SimulationCharacter::processMessage(DataSerialized& message)
{
    std::string messageType;
    message.loadString(messageType);

    if (messageType == "msg_request_interaction")
    {
        if (isPlayer(_data))
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
        if (isPlayer(_data))
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
        if (isPlayer(_data))
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

void SimulationCharacter::teleportToPosition(vec3 position)
{
    physengine::setCharacterPosition(*_data->cpd, position);
}

void SimulationCharacter::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);

    glm_vec3_copy(pos, _data->cpd->currentCOMPosition);
    physengine::setCharacterPosition(*_data->cpd, pos);
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

void defaultRenderImGui(SimulationCharacter_XData* d)
{
    if (ImGui::CollapsingHeader("Tweak Props", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::CollapsingHeader("Backend Movement", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat3("inputVelocity", d->bmis.inputVelocity);
            ImGui::DragFloat("verticalVelocity", &d->bmis.verticalVelocity);
            ImGui::DragFloat("cosMaxGroundSlopeAngle", &d->bmis.cosMaxGroundSlopeAngle);
            ImGui::DragFloat("cosMaxCeilingSlopeAngle", &d->bmis.cosMaxCeilingSlopeAngle);
            ImGui::DragFloat("stickToGroundMaxDelta", &d->bmis.stickToGroundMaxDelta);
            ImGui::DragFloat3("gravityDirection", d->bmis.gravityDirection);
            ImGui::DragFloat("gravityMagnitude", &d->bmis.gravityMagnitude);

            ImGui::Checkbox("isGrounded", &d->bmis.isGrounded);
            ImGui::Checkbox("hitCeiling", &d->bmis.hitCeiling);
            ImGui::Checkbox("attemptStickToGround", &d->bmis.attemptStickToGround);
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Frontend Movement", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat("maxSpeed", &d->fmis.maxSpeed);
            ImGui::DragFloat("currentSpeed", &d->fmis.currentSpeed);
            ImGui::DragFloat2("groundedAccelDecel", d->fmis.groundedAccelDecel);
            ImGui::DragFloat2("negGroundedAccelDecel", d->fmis.negGroundedAccelDecel);
            ImGui::DragFloat2("airborneAccelDecel", d->fmis.airborneAccelDecel);
            ImGui::DragFloat("turningSpeed", &d->fmis.turningSpeed);
        }

        ImGui::Separator();

        ImGui::DragFloat("modelSize", &d->modelSize);
        ImGui::DragFloat("jumpHeight", &d->jumpHeight);
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

void updateHitscanLaunchVeloRelPosExportString(SimulationCharacter_XData* d)
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

void attackWazaEditorRenderImGui(SimulationCharacter_XData* d)
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
            SimulationCharacter_XData::AttackWaza& aw = d->attackWazaEditor.editingWazaSet[i];
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

void SimulationCharacter::renderImGui()
{
    if (_data->attackWazaEditor.isEditingMode)
        attackWazaEditorRenderImGui(_data);
    else
        defaultRenderImGui(_data);
}

void SimulationCharacter::reportPhysicsContact(const JPH::Body& otherBody, const JPH::ContactManifold& manifold, JPH::ContactSettings* ioSettings)
{
    JPH::Vec3 attachmentNormal = -manifold.mWorldSpaceNormal;
    bool isSlopeTooSteep = physengine::isSlopeTooSteepForCharacter(*_data->cpd, attachmentNormal);
    if (!_data->prevGroundNormalSet && !isSlopeTooSteep)
    {
        glm_vec3_copy(
            vec3{ attachmentNormal.GetX(), attachmentNormal.GetY(), attachmentNormal.GetZ() },
            _data->prevGroundNormal
        );
        _data->prevGroundNormalSet = true;
    }

    SimulationCharacter_XData::MovingPlatformAttachment& mpa = _data->movingPlatformAttachment;

    if (otherBody.IsStatic())
    {
        mpa.attachmentStage = SimulationCharacter_XData::MovingPlatformAttachment::AttachmentStage::NO_ATTACHMENT;
        return;
    }

    if (isSlopeTooSteep)
    {
        mpa.attachmentStage = SimulationCharacter_XData::MovingPlatformAttachment::AttachmentStage::NO_ATTACHMENT;
        return;
    }

    // std::cout << "ATT NORM: " << attachmentNormal.GetX() << ",\t" <<  attachmentNormal.GetY() << ",\t" <<  attachmentNormal.GetZ() << ",\t" << std::endl;

    if (mpa.attachmentStage == SimulationCharacter_XData::MovingPlatformAttachment::AttachmentStage::NO_ATTACHMENT ||
        mpa.attachedBodyId != otherBody.GetID())
    {
        // Initial attachment.
        mpa.attachmentStage = SimulationCharacter_XData::MovingPlatformAttachment::AttachmentStage::INITIAL_ATTACHMENT;
        mpa.attachedBodyId = otherBody.GetID();
    }
    else
    {
        // Calc where in the attachment amortization chain.
        if (mpa.attachmentStage != SimulationCharacter_XData::MovingPlatformAttachment::AttachmentStage::RECURRING_ATTACHMENT)
            mpa.attachmentStage = SimulationCharacter_XData::MovingPlatformAttachment::AttachmentStage((int32_t)mpa.attachmentStage + 1);

        // This is past the initial attachment! Calculate how much has moved.
        JPH::RVec3 attachmentDeltaPos = otherBody.GetWorldTransform() * mpa.attachmentPositionLocal - mpa.attachmentPositionWorld;
        mpa.nextDeltaPosition[0] = attachmentDeltaPos[0];
        mpa.nextDeltaPosition[1] = attachmentDeltaPos[1];
        mpa.nextDeltaPosition[2] = attachmentDeltaPos[2];
    }

    // Calculate attachment to body!
    mpa.attachmentPositionWorld = manifold.GetWorldSpaceContactPointOn1(0) + _data->cpd->radius * attachmentNormal;  // Suck it into the capsule's base sphere origin point.
    mpa.attachmentPositionLocal = otherBody.GetWorldTransform().Inversed() * mpa.attachmentPositionWorld;
    mpa.attachmentYAxisAngularVelocity = otherBody.GetAngularVelocity().GetY();

    // ioSettings->mCombinedFriction = 1000.0f;

    mpa.attachmentIsStale = false;
}

RenderObject* SimulationCharacter::getMainRenderObject()
{
    return _data->characterRenderObj;
}
