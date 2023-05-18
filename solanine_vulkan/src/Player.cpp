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

    vec3 worldSpaceInput = GLM_VEC3_ZERO_INIT;

    // Tweak Props
    vec3 position;
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
    vec2 input = GLM_VEC2_ZERO_INIT;
    input[0] += input::keyLeftPressed  ? -1.0f : 0.0f;
    input[0] += input::keyRightPressed ?  1.0f : 0.0f;
    input[1] += input::keyUpPressed    ?  1.0f : 0.0f;
    input[1] += input::keyDownPressed  ? -1.0f : 0.0f;

    if (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
    {
        input[0] = input[1] = 0.0f;
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

    if (glm_vec3_dot(_data->worldSpaceInput, _data->worldSpaceInput) < 0.01f)
        _data->worldSpaceInput[0] = _data->worldSpaceInput[1] = _data->worldSpaceInput[2] = 0.0f;
    else
    {
        _data->worldSpaceInput[0] = glm_clamp_zo(_data->worldSpaceInput[0]);
        _data->worldSpaceInput[1] = glm_clamp_zo(_data->worldSpaceInput[1]);
        _data->worldSpaceInput[2] = glm_clamp_zo(_data->worldSpaceInput[2]);
    }


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
    vec2 input = GLM_VEC2_ZERO_INIT;  // @COPYPASTA
    input[0] += input::keyLeftPressed ? -1.0f : 0.0f;
    input[0] += input::keyRightPressed ? 1.0f : 0.0f;
    input[1] += input::keyUpPressed ? 1.0f : 0.0f;
    input[1] += input::keyDownPressed ? -1.0f : 0.0f;

    if (_data->camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
    {
        input[0] = input[1] = 0.0f;
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

    // Update render transform
    if (glm_vec3_dot(_data->worldSpaceInput, _data->worldSpaceInput) > 0.01f)
        _data->facingDirection = atan2f(_data->worldSpaceInput[0], _data->worldSpaceInput[2]);

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
    vec3 velocity;
    glm_vec3_copy(_data->worldSpaceInput, velocity);
    vec3 verticalMovement = {
        0.0f,
        (input::keyWorldUpPressed ? 1.0f : 0.0f) + (input::keyWorldDownPressed ? -1.0f : 0.0f),
        0.0f,
    };
    glm_vec3_add(velocity, verticalMovement, velocity);
    glm_vec3_scale(velocity, 0.1f, velocity);
    physengine::moveCapsuleAccountingForCollision(*_data->cpd, velocity);
    glm_vec3_copy(_data->cpd->basePosition, _data->position);
}

void Player::lateUpdate(const float_t& deltaTime)
{
    //
    // Update position of character and weapon
    //
    vec3 interpPos;
    glm_vec3_copy(_data->position, interpPos);  //physutil::getPosition(_physicsObj->interpolatedTransform);
    vec3 eulerAngles = { 0.0f, _data->facingDirection, 0.0f };
    mat4 rotation;
    glm_euler(eulerAngles, rotation);
    vec3 scale = { _data->modelSize, _data->modelSize, _data->modelSize };

    mat4 transform = GLM_MAT4_IDENTITY_INIT;
    glm_translate(transform, interpPos);
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

bool Player::processMessage(DataSerialized& message)
{
    return false;
}

void Player::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);
    glm_vec3_copy(pos, _data->position);
}

void Player::renderImGui()
{
    ImGui::DragFloat("modelSize", &_data->modelSize);
}
