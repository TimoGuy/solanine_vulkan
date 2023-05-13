#include "Player.h"

#include "PhysUtil.h"
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


Player::Player(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds) : Entity(em, ds), _rom(rom), _camera(camera)
{
    if (ds)
        load(*ds);

    _weaponAttachmentJointName = "Back Attachment";
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

    vkglTF::Model* characterModel = _rom->getModel("SlimeGirl", this, [](){});
    _characterRenderObj =
        _rom->registerRenderObject({
            .model = characterModel,
            .animator = new vkglTF::Animator(characterModel, animatorCallbacks),
            .transformMatrix = glm::translate(glm::mat4(1.0f), _load_position) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0))),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    vkglTF::Model* handleModel = _rom->getModel("Handle", this, [](){});
    _handleRenderObj =
        _rom->registerRenderObject({
            .model = handleModel,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    vkglTF::Model* weaponModel = _rom->getModel("WingWeapon", this, [](){});
    _weaponRenderObj =
        _rom->registerRenderObject({
            .model = weaponModel,
            .renderLayer = RenderLayer::INVISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    _camera->mainCamMode.setMainCamTargetObject(_characterRenderObj);  // @NOTE: I believe that there should be some kind of main camera system that targets the player by default but when entering different volumes etc. the target changes depending.... essentially the system needs to be more built out imo


    _enablePhysicsUpdate = true;
    _enableUpdate = true;
    _enableLateUpdate = true;
}

Player::~Player()
{
    delete _characterRenderObj->animator;
    _rom->unregisterRenderObject(_characterRenderObj);
    _rom->unregisterRenderObject(_handleRenderObj);
    _rom->unregisterRenderObject(_weaponRenderObj);
    _rom->removeModelCallbacks(this);

    // @TODO: figure out if I need to call `delete _collisionShape;` or not
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

    if (_camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
    {
        input = glm::vec2(0.0f);
    }

    glm::vec3 flatCameraFacingDirection = _camera->sceneCamera.facingDirection;
    flatCameraFacingDirection.y = 0.0f;
    flatCameraFacingDirection = glm::normalize(flatCameraFacingDirection);

    _worldSpaceInput =
        input.y * flatCameraFacingDirection +
        input.x * glm::normalize(glm::cross(flatCameraFacingDirection, glm::vec3(0, 1, 0)));

    if (glm::length2(_worldSpaceInput) < 0.01f)
        _worldSpaceInput = glm::vec3(0.0f);
    else
        _worldSpaceInput = physutil::clampVector(_worldSpaceInput, 0.0f, 1.0f);


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

    if (_camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
    {
        input = glm::vec2(0.0f);
    }

    glm::vec3 flatCameraFacingDirection = _camera->sceneCamera.facingDirection;
    flatCameraFacingDirection.y = 0.0f;
    flatCameraFacingDirection = glm::normalize(flatCameraFacingDirection);

    _worldSpaceInput =
        input.y * flatCameraFacingDirection +
        input.x * glm::normalize(glm::cross(flatCameraFacingDirection, glm::vec3(0, 1, 0)));

    // Update render transform
    if (glm::length2(_worldSpaceInput) > 0.01f)
        _facingDirection = glm::atan(_worldSpaceInput.x, _worldSpaceInput.z);

    //
    // Update mask for animation
    // @TODO: there is popping for some reason. Could be how the transitions/triggers work in the game or could be a different underlying issue. Figure it out pls!  -Timo
    //
    _characterRenderObj->animator->setMask(
        "MaskCombatMode",
        false
    );
}

void Player::lateUpdate(const float_t& deltaTime)
{
    //
    // Update position of character and weapon
    //
    glm::vec3 interpPos                  = glm::vec3(0.0f);  //physutil::getPosition(_physicsObj->interpolatedTransform);
    _characterRenderObj->transformMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0)));

    glm::mat4 attachmentJointMat         = _characterRenderObj->animator->getJointMatrix(_weaponAttachmentJointName);
    _weaponRenderObj->transformMatrix    = _characterRenderObj->transformMatrix * attachmentJointMat;
    _handleRenderObj->transformMatrix    = _weaponRenderObj->transformMatrix;
}

void Player::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(physutil::getPosition(_characterRenderObj->transformMatrix));
    ds.dumpFloat(_facingDirection);
}

void Player::load(DataSerialized& ds)
{
    Entity::load(ds);
    _load_position         = ds.loadVec3();
    _facingDirection       = ds.loadFloat();
}

bool Player::processMessage(DataSerialized& message)
{
    return false;
}

void Player::reportMoved(void* matrixMoved)
{
    return;
}

void Player::renderImGui()
{
    
}
