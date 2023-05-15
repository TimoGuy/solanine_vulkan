#include "Player.h"

#include "Imports.h"
#include "PhysUtil.h"
#include "PhysicsEngine.h"
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "EntityManager.h"
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

    glm::vec3 worldSpaceInput = glm::vec3(0.0f);

    // Tweak Props
    glm::vec3 position;
    float_t facingDirection = 0.0f;
    float_t modelSize = 0.3f;
};


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
            .transformMatrix = glm::translate(glm::mat4(1.0f), _data->position) * glm::toMat4(glm::quat(glm::vec3(0, _data->facingDirection, 0))) * glm::scale(glm::mat4(1.0f), glm::vec3(_data->modelSize)),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

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
}

Player::~Player()
{
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
    //
    // Calculate input
    //
    glm::vec2 input(0.0f);
    input.x += input::keyLeftPressed  ? -1.0f : 0.0f;
    input.x += input::keyRightPressed ?  1.0f : 0.0f;
    input.y += input::keyUpPressed    ?  1.0f : 0.0f;
    input.y += input::keyDownPressed  ? -1.0f : 0.0f;

    if (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
    {
        input = glm::vec2(0.0f);
    }

    glm::vec3 flatCameraFacingDirection = _data->camera->sceneCamera.facingDirection;
    flatCameraFacingDirection.y = 0.0f;
    flatCameraFacingDirection = glm::normalize(flatCameraFacingDirection);

    _data->worldSpaceInput =
        input.y * flatCameraFacingDirection +
        input.x * glm::normalize(glm::cross(flatCameraFacingDirection, glm::vec3(0, 1, 0)));

    if (glm::length2(_data->worldSpaceInput) < 0.01f)
        _data->worldSpaceInput = glm::vec3(0.0f);
    else
        _data->worldSpaceInput = physutil::clampVector(_data->worldSpaceInput, 0.0f, 1.0f);


    //
    // Update state
    //
    ;;;;;;;;;;;;;;;;;;;;;;;
}

void Player::update(const float_t& deltaTime)
{
    //
    // Calculate render object transform
    //
    glm::vec2 input(0.0f);  // @COPYPASTA
    input.x += input::keyLeftPressed  ? -1.0f : 0.0f;
    input.x += input::keyRightPressed ?  1.0f : 0.0f;
    input.y += input::keyUpPressed    ?  1.0f : 0.0f;
    input.y += input::keyDownPressed  ? -1.0f : 0.0f;

    if (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
    {
        input = glm::vec2(0.0f);
    }

    glm::vec3 flatCameraFacingDirection = _data->camera->sceneCamera.facingDirection;
    flatCameraFacingDirection.y = 0.0f;
    flatCameraFacingDirection = glm::normalize(flatCameraFacingDirection);

    _data->worldSpaceInput =
        input.y * flatCameraFacingDirection +
        input.x * glm::normalize(glm::cross(flatCameraFacingDirection, glm::vec3(0, 1, 0)));

    // Update render transform
    if (glm::length2(_data->worldSpaceInput) > 0.01f)
        _data->facingDirection = glm::atan(_data->worldSpaceInput.x, _data->worldSpaceInput.z);

    //
    // Update mask for animation
    // @TODO: there is popping for some reason. Could be how the transitions/triggers work in the animator controller or could be a different underlying issue. Figure it out pls!  -Timo
    //
    _data->characterRenderObj->animator->setMask(
        "MaskCombatMode",
        false
    );

    //
    // @DEBUG: @TEST: try doing some voxel collision
    //
    glm::vec3 velocity = glm::vec3(0.0f);
    velocity += _data->worldSpaceInput;
    velocity += glm::vec3(0, input::keyWorldUpPressed ? 1.0f : 0.0f, 0);
    velocity += glm::vec3(0, input::keyWorldDownPressed ? -1.0f : 0.0f, 0);
    velocity *= 0.1f;
    physengine::moveCapsuleAccountingForCollision(*_data->cpd, velocity);
    _data->position = _data->cpd->basePosition;
}

void Player::lateUpdate(const float_t& deltaTime)
{
    //
    // Update position of character and weapon
    //
    glm::vec3 interpPos                        = _data->position;  //physutil::getPosition(_physicsObj->interpolatedTransform);
    _data->characterRenderObj->transformMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::toMat4(glm::quat(glm::vec3(0, _data->facingDirection, 0))) * glm::scale(glm::mat4(1.0f), glm::vec3(_data->modelSize));

    glm::mat4 attachmentJointMat               = _data->characterRenderObj->animator->getJointMatrix(_data->weaponAttachmentJointName);
    _data->weaponRenderObj->transformMatrix    = _data->characterRenderObj->transformMatrix * attachmentJointMat;
    _data->handleRenderObj->transformMatrix    = _data->weaponRenderObj->transformMatrix;
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
    _data->position         = ds.loadVec3();
    _data->facingDirection       = ds.loadFloat();
}

bool Player::processMessage(DataSerialized& message)
{
    return false;
}

void Player::reportMoved(void* matrixMoved)
{
    _data->position = physutil::getPosition(*(glm::mat4*)matrixMoved);
}

void Player::renderImGui()
{
    ImGui::DragFloat("modelSize", &_data->modelSize);
}
