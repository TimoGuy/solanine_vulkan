#include "Player.h"

#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "Camera.h"
#include "InputManager.h"
#include "AudioEngine.h"
#include "DataSerialization.h"
#include "Debug.h"
#include "imgui/imgui.h"
#include "Yosemite.h"


Player::Player(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds) : Entity(em, ds), _rom(rom), _camera(camera)
{
    if (ds)
        load(*ds);

    _weaponAttachmentJointName = "Back Attachment";
    std::vector<vkglTF::Animator::AnimatorCallback> animatorCallbacks = {
        {
            "EventSwitchToBackAttachment", [&]() {
                _weaponAttachmentJointName = "Back Attachment";
                _isWeaponDrawn = false;
            }
        },
        {
            "EventSwitchToHandAttachment", [&]() {
                _weaponAttachmentJointName = "Hand Attachment";
                _isWeaponDrawn = true;
            }
        },
        {
            "EventPlaySFXMaterialize", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    // "res/sfx/wip_draw_weapon.ogg",
                    "res/sfx/wip_poweron1.wav",
                    "res/sfx/wip_poweron2.wav",
                    "res/sfx/wip_poweron3.wav",
                });
                _weaponRenderObj->renderLayer = RenderLayer::VISIBLE;
            }
        },
        {
            "EventPlaySFXBreakoff", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    // "res/sfx/wip_sheath_weapon.ogg",
                    // "res/sfx/wip_sheath_weapon_2.ogg",
                    "res/sfx/wip_poweroff1.wav",
                    "res/sfx/wip_poweroff2.wav",
                    "res/sfx/wip_poweroff3.wav",
                });
                _weaponRenderObj->renderLayer = RenderLayer::INVISIBLE;
            }
        },
        {
            "EventPlaySFXAttack", [&]() {
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_MM_Link_Attack1.wav",
                    "res/sfx/wip_MM_Link_Attack2.wav",
                    "res/sfx/wip_MM_Link_Attack3.wav",
                    "res/sfx/wip_MM_Link_Attack4.wav",
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
            "EventAllowComboInput", [&]() {
                _allowComboInput = true;
            }
        },
        {
            "EventAllowComboTransition", [&]() {
                _allowComboTransition = true;
            }
        },
        {
            "EventGotoEndAttackStage", [&]() {
                _attackStage = AttackStage::END;
            }
        },
        {
            "EventEndAttack", [&]() {
                _attackStage = AttackStage::NONE;
                _flagAttack = false;  // To prevent unusual behavior (i.e. had a random attack just start from the beginning despite no inputs. So this is just to make sure)
            }
        },
        {
            "EventEnableMCMLayer", [&]() {
                _characterRenderObj->animator->setMask("MaskCombatMode", true);
            }
        },
        {
            "EventDisableMCMLayer", [&]() {
                _characterRenderObj->animator->setMask("MaskCombatMode", false);
            }
        },
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

    _totalHeight = 5.0f;
    _maxClimbAngle = glm::radians(47.0f);

    _capsuleRadius = 0.5f;
    //float_t d = (r - r * glm::sin(_maxClimbAngle)) / glm::sin(_maxClimbAngle);  // This is the "perfect algorithm", but we want stair stepping abilities too...
    constexpr float_t raycastMargin = 0.05f;
    _bottomRaycastFeetDist = 2.0f + raycastMargin;
    _bottomRaycastExtraDist = 1.0f + raycastMargin;

    _collisionShape = new btCapsuleShape(_capsuleRadius, _totalHeight - _bottomRaycastFeetDist);  // @NOTE: it appears that this shape has a margin in the direction of the sausage (i.e. Y in this case) and then the radius is the actual radius
    _adjustedHalfHeight = (_totalHeight - _bottomRaycastFeetDist) * 0.5f + _collisionShape->getMargin();

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            1.0f,
            _load_position,
            glm::quat(glm::vec3(0.0f)),
            _collisionShape,
            &getGUID()
        );
    _physicsObj->transformOffset = glm::vec3(0, -4, 0);
    auto body = _physicsObj->body;
    body->setAngularFactor(0.0f);
    body->setDamping(0.0f, 0.0f);
    body->setFriction(0.0f);
    body->setActivationState(DISABLE_DEACTIVATION);

    _onCollisionStayFunc =
        [&](btPersistentManifold* manifold, bool amIB) { onCollisionStay(manifold, amIB); };
    _physicsObj->onCollisionStayCallback = &_onCollisionStayFunc;

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
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    // @TODO: figure out if I need to call `delete _collisionShape;` or not
}

void Player::update(const float_t& deltaTime)
{
    if (input::onKeyJumpPress)
    {
        _flagJump = true;
        _jumpInputBufferFramesTimer = _jumpInputBufferFrames;
    }

    if (input::onKeyF9Press)
    {
        if (_recordingState != RecordingState::RECORDING)
        {
            _recordingState = RecordingState::RECORDING;

            // Start the recording
            glm::vec3 sp = physutil::toVec3(_physicsObj->body->getWorldTransform().getOrigin());
            float_t   sd = _facingDirection;
            _replayData.startRecording(sp, sd);

            debug::pushDebugMessage({
                .message = "Started Recording",
            });
        }
        else
        {
            _recordingState = RecordingState::NONE;
            
            size_t recordingSize = _replayData.getRecordingSize();

            debug::pushDebugMessage({
                .message = "Stopped Recording. Current Length is " + std::to_string(recordingSize) + " bytes",
            });
        }
    }

    if (input::onKeyF8Press)
    {
        if (_recordingState != RecordingState::PLAYING)
        {
            _recordingState = RecordingState::PLAYING;

            // Start the playback
            glm::vec3 sp;
            float_t   sd;
            _replayData.playRecording(sp, sd);

            _physicsObj->body->setWorldTransform(
                btTransform(btQuaternion::getIdentity(), btVector3(sp.x, sp.y, sp.z))
            );
            _facingDirection = sd;

            debug::pushDebugMessage({
                .message = "Started Playing Recording",
            });
        }
        else
        {
            _recordingState = RecordingState::NONE;

            debug::pushDebugMessage({
                .message = "Stopped Playing Recording Midway",
            });
        }
    }

    if (input::onRMBPress)
    {
        _flagDrawOrSheathWeapon = true;
    }

    if (input::onLMBPress && _isWeaponDrawn)
    {
        _flagAttack = true;
    }

    //
    // Calculate render object transform
    //
    if (_recordingState != RecordingState::PLAYING)
    {
        glm::vec2 input(0.0f);  // @COPYPASTA
        input.x += input::keyLeftPressed  ? -1.0f : 0.0f;
        input.x += input::keyRightPressed ?  1.0f : 0.0f;
        input.y += input::keyUpPressed    ?  1.0f : 0.0f;
        input.y += input::keyDownPressed  ? -1.0f : 0.0f;

        if (_camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
            input = glm::vec2(0.0f);

        if (_isWeaponDrawn && _attackStage != AttackStage::NONE)
        {
            input = glm::vec2(0);
        }

        glm::vec3 flatCameraFacingDirection = _camera->sceneCamera.facingDirection;
        flatCameraFacingDirection.y = 0.0f;
        flatCameraFacingDirection = glm::normalize(flatCameraFacingDirection);

        _worldSpaceInput =
            input.y * flatCameraFacingDirection +
            input.x * glm::normalize(glm::cross(flatCameraFacingDirection, glm::vec3(0, 1, 0)));
    }

    // Update render transform
    if (glm::length2(_worldSpaceInput) > 0.01f)
        _facingDirection = glm::atan(_worldSpaceInput.x, _worldSpaceInput.z);
}

void Player::lateUpdate(const float_t& deltaTime)
{
    glm::vec3 interpPos                  = physutil::getPosition(_physicsObj->interpolatedTransform);
    _characterRenderObj->transformMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0)));

    glm::mat4 attachmentJointMat         = _characterRenderObj->animator->getJointMatrix(_weaponAttachmentJointName);
    _weaponRenderObj->transformMatrix    = _characterRenderObj->transformMatrix * attachmentJointMat;
    _handleRenderObj->transformMatrix    = _weaponRenderObj->transformMatrix;
}

void Player::physicsUpdate(const float_t& physicsDeltaTime)
{
    //
    // Clear state
    //
    _onGround = false;

    //
    // Calculate input
    //
    if (_recordingState != RecordingState::PLAYING)
    {
        glm::vec2 input(0.0f);
        input.x += input::keyLeftPressed  ? -1.0f : 0.0f;
        input.x += input::keyRightPressed ?  1.0f : 0.0f;
        input.y += input::keyUpPressed    ?  1.0f : 0.0f;
        input.y += input::keyDownPressed  ? -1.0f : 0.0f;

        if (_camera->freeCamMode.enabled || ImGui::GetIO().WantTextInput)  // @DEBUG: for the level editor
        {
            input = glm::vec2(0.0f);
            _flagJump = false;
            _flagAttack = false;
            _flagDrawOrSheathWeapon = false;
        }

        if (_isWeaponDrawn && _attackStage != AttackStage::NONE)
        {
            input = glm::vec2(0);
            _flagDrawOrSheathWeapon = false;
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
    }

    //
    // Record/Play Replay Data
    //
    if (_recordingState == RecordingState::RECORDING)
    {
        _replayData.recordStep(glm::vec2(_worldSpaceInput.x, _worldSpaceInput.z), _flagJump);
    }
    else if (_recordingState == RecordingState::PLAYING)
    {
        glm::vec2 wsi;
        if (_replayData.playRecordingStep(wsi, _flagJump))
        {
            _recordingState = RecordingState::NONE;

            debug::pushDebugMessage({
                .message = "Recording Finished",
            });
        }
        else
        {
            _worldSpaceInput = glm::vec3(wsi.x, 0, wsi.y);
        }
    }

    //
    // Update state
    //
    _stepsSinceLastGrounded++;
    _framesSinceAttachedBody++;

    glm::vec3 velocity = physutil::toVec3(_physicsObj->body->getLinearVelocity());

    velocity -= (_displacementToTarget + _attachmentVelocity) / physicsDeltaTime;  // Undo the displacement (hopefully no movement bugs)
    _displacementToTarget = glm::vec3(0.0f);
    // _attachmentVelocity   = glm::vec3(0.0f);

    float groundAccelMult;
    processGrounded(velocity, groundAccelMult, physicsDeltaTime);

    if (_flagDrawOrSheathWeapon)
    {
        // Reset flags
        _flagDrawOrSheathWeapon = false;

        /*if (!_isWeaponDrawn && _isSprinting)  @TODO: feature request, to have sprinting
        {
            // Start grounded attack immediately
        }
        else*/ if (!_isWeaponDrawn && !_onGround)
        {
            _airDashMove = false;  // @NOTE: I added this back in like 5 minutes after removing it bc the 45deg air dash is with the sword drawn, so it makes sense to nullify it if you're gonna sheath your weapon.  -Timo 2022/11/12    @NOTE: I think that removing this is important so that you can do the airborne sword drawing airdash and then put your sword away to jump easier  -Timo 2022/11/12
            _characterRenderObj->animator->runEvent("EventPlaySFXMaterialize");
            startAttack(AttackType::DIVE_ATTACK);
        }
        else
        {
            //
            // Enter/exit combat mode
            //
            _attackStage = AttackStage::NONE;
            if (_isWeaponDrawn)
                _characterRenderObj->animator->setTrigger("leave_combat_mode");
            else
                _characterRenderObj->animator->setTrigger("goto_combat_mode");
        }
    }

    if (_airDashMove)
    {
        //
        // Process air dash
        //
        velocity = _airDashDirection * physutil::lerp(_airDashSpeed, _airDashSpeed * _airDashFinishSpeedFracCooked, _airDashTimeElapsed / _airDashTime);

        // First frame of actual dash, play sound
        if (_airDashTimeElapsed == 0.0f)
            AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_char_mad_dash_red_left.ogg",
                "res/sfx/wip_char_mad_dash_red_right.ogg",
                });

        _airDashTimeElapsed += physicsDeltaTime;

        // Exit air dash
        if (_onGround || _airDashTimeElapsed > _airDashTime)
        {
            _airDashMove = false;
        }
    }
    
    if (_isWeaponDrawn)
    {
        //
        // Process combat mode
        //
        switch (_attackStage)
        {
        case AttackStage::NONE:
        {
            if (_flagAttack)
            {
                startAttack(
                    _onGround ? AttackType::HORIZONTAL : AttackType::DIVE_ATTACK  // @NOTE: weapon is already drawn at this point
                );
            }
            break;
        }

        case AttackStage::PREPAUSE:
        {
            /*if (_attackPrepauseTimeElapsed == 0.0f)
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_OOT_YoungLink_Grunt.wav",
                    });*/

            if (_attackPrepauseTimeElapsed < _attackPrepauseTime)
            {
                if (_attackType == AttackType::DIVE_ATTACK)
                    velocity = glm::vec3(0);
                _attackPrepauseTimeElapsed += physicsDeltaTime;
            }
            else
            {
                _attackStage = AttackStage::SWING;
                _attackSwingTimeElapsed = 0.0f;
            }

            // Reset flags in preparation for next step
            _flagAttack = false;
            _weaponPrevTransform = glm::mat4(0);

            if (!_usedSpinAttack && _flagJump)  // Check jump flag before flag gets nuked
            {
                // Switch attack type to spin attack
                _attackStage = AttackStage::SWING;
                _attackType = AttackType::SPIN_ATTACK;
                _attackSwingTimeElapsed = 0.0f;
            }

            // Apply next animation
            if (_attackStage == AttackStage::SWING)
            {
                if (_attackType == AttackType::HORIZONTAL)
                    _characterRenderObj->animator->setTrigger("goto_horizontal_attack_swing");
                else if (_attackType == AttackType::DIVE_ATTACK)
                    _characterRenderObj->animator->setTrigger("goto_dive_attack_swing");
                else if (_attackType == AttackType::SPIN_ATTACK)
                    _characterRenderObj->animator->setTrigger("goto_spin_attack_swing");
            }

            break;
        }

        case AttackStage::SWING:
            processAttackStageSwing(velocity, physicsDeltaTime);
            processWeaponCollision();
            break;

        case AttackStage::CHAIN_COMBO:
            // @TODO: stub
            break;

        case AttackStage::END:
            // @TODO: stub
            break;
        }
    }

    //
    // Calculate rigidbody velocity
    // 
    // @NOTE: it seems like the current methodology is to make a physically accurate
    //        character collider. That would work, but it's kinda a little weird how
    //        the character slowly slides down ramps or can't go up a ramp too. Maybe
    //        some things could be done to change that, but landing on a ramp and sliding
    //        down until you regain your X and Z is pretty cool. Hitting a nick in the
    //        ground and flying up is pretty okay too, though I wish it didn't happen
    //        so dramatically with higher speeds, but maybe keeping the speed at 20 or
    //        so is the best bc the bump up really isn't that noticable, however, it's like
    //        the character tripped because of the sudden velocity speed drop when running
    //        into a nick.
    //
    glm::vec3 desiredVelocity = _worldSpaceInput * _maxSpeed;  // @NOTE: we just ignore the y component in this desired velocity thing

    glm::vec2 a(velocity.x, velocity.z);
    glm::vec2 b(desiredVelocity.x, desiredVelocity.z);

    if (_onGround)
        if (glm::length2(b) < 0.0001f)
            _characterRenderObj->animator->setTrigger("goto_idle");
        else
            _characterRenderObj->animator->setTrigger("goto_run");

    bool useAcceleration;
    if (glm::length2(b) < 0.0001f)
        useAcceleration = false;
    else if (glm::length2(a) < 0.0001f)
        useAcceleration = true;
    else
    {
        float_t AdotB = glm::dot(glm::normalize(a), glm::normalize(b));
        if (glm::length(a) * AdotB > glm::length(b))    // @TODO: use your head and think of how to use length2 for this
            useAcceleration = false;
        else
            useAcceleration = true;
    }

    float_t acceleration    = _onGround ? _maxAcceleration * groundAccelMult : _maxMidairAcceleration;
    if (!useAcceleration)
        acceleration        = _onGround ? _maxDeceleration * groundAccelMult : _maxMidairDeceleration;
    float_t maxSpeedChange  = acceleration * physicsDeltaTime;

    glm::vec2 c = physutil::moveTowardsVec2(a, b, maxSpeedChange);
    velocity.x = c.x;
    velocity.z = c.y;

    if (_isWeaponDrawn)
        _flagJump = false;

    if (_flagJump)
    {
        //
        // Do the normal jump
        //
        enum JumpType
        {
            GROUNDED_JUMP,
            AIR_DASH,
            NONE,
        } jumpType;

        jumpType = (_onGround || (int32_t)_stepsSinceLastGrounded <= _jumpCoyoteFrames) ? GROUNDED_JUMP : (_usedAirDash ? NONE : AIR_DASH);

        bool jumpFlagProcessed = false;
        switch (jumpType)
        {
        case GROUNDED_JUMP:
            if (_onGround || (int32_t)_stepsSinceLastGrounded <= _jumpCoyoteFrames)
            {
                // @DEBUG: if you want something to look at coyote time and jump buffering metrics, uncomment:
                //std::cout << "[JUMP INFO]" << std::endl
                //    << "Buffer Frames left:         " << _jumpInputBufferFramesTimer << std::endl
                //    << "Frames since last grounded: " << _stepsSinceLastGrounded << std::endl;
                velocity.y = 
                    glm::sqrt(_jumpHeight * 2.0f * PhysicsEngine::getInstance().getGravityStrength());
                _displacementToTarget = glm::vec3(0.0f);
                _stepsSinceLastGrounded = _jumpCoyoteFrames;  // This is to prevent ground sticking right after a jump and multiple jumps performed right after another jump was done!
                
                if (!_isAttachedBodyStale)
                {
                    // Apply jump pushaway to attached body
                    btVector3 relPos = physutil::toVec3(_attachmentWorldPosition) - _attachedBody->getWorldTransform().getOrigin();
                    _attachedBody->applyForce(
                        physutil::toVec3(-velocity) * _physicsObj->body->getMass() * _landingApplyMassMult,
                        relPos
                    );
                }

                // @TODO: add some kind of audio event system, or even better, figure out how to use FMOD!!! Bc it's freakign integrated lol
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_jump1.ogg",
                    "res/sfx/wip_jump2.ogg"
                    });

                jumpFlagProcessed = true;                    
            }
            break;

        case AIR_DASH:
        {
            // @TODO: you're gonna have to check for jump buffer time for this bc there is a chance that the player is intending to jump on the ground despite having
            //        a jump they can do in the air. You will need to detect whether they are too close to the ground to store the jump input rather than do it as a
            //        air dash  -Timo
            if (glm::length2(_worldSpaceInput) > 0.0001f)
            {
                _airDashDirection = glm::normalize(_worldSpaceInput);
            }
            else
            {
                _airDashDirection = glm::quat(glm::vec3(0, _facingDirection, 0)) * glm::vec3(0, 0, 1);
            }

            _airDashMove = true;
            _usedAirDash = true;
            _airDashTimeElapsed = 0.0f;
            _airDashFinishSpeedFracCooked = _airDashFinishSpeedFrac;

            jumpFlagProcessed = true;

            break;
        }

        case NONE:
            _flagJump = false;
            break;
        }

        // Turn off flag for sure if successfully jumped
        if (jumpFlagProcessed)
        {
            _jumpPreventOnGroundCheckFramesTimer = _jumpPreventOnGroundCheckFrames;
            _jumpInputBufferFramesTimer = -1;
            _flagJump = false;
        }

        // Turn off flag if jump buffer frames got exhausted
        if (_jumpInputBufferFramesTimer-- < 0)
            _flagJump = false;
    }

    _physicsObj->body->setLinearVelocity(physutil::toVec3(velocity + (_displacementToTarget + _attachmentVelocity) / physicsDeltaTime));
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

void Player::reportMoved(void* matrixMoved)
{
    _physicsObj->reportMoved(
        glm::translate(*(glm::mat4*)matrixMoved, -_physicsObj->transformOffset),
        true
    );
}

void Player::renderImGui()
{
    ImGui::Text(("_onGround: " + std::to_string(_onGround)).c_str());
    ImGui::DragFloat("_maxSpeed", &_maxSpeed);
    ImGui::DragFloat("_maxAcceleration", &_maxAcceleration);
    ImGui::DragFloat("_maxDeceleration", &_maxDeceleration);
    ImGui::DragFloat("_maxMidairAcceleration", &_maxMidairAcceleration);
    ImGui::DragFloat("_maxMidairDeceleration", &_maxMidairDeceleration);
    ImGui::DragFloat("_jumpHeight", &_jumpHeight);
    ImGui::DragFloat3("_physicsObj->transformOffset", &_physicsObj->transformOffset[0]);
    ImGui::DragInt("_jumpPreventOnGroundCheckFrames", &_jumpPreventOnGroundCheckFrames, 1.0f, 0, 10);
    ImGui::DragInt("_jumpCoyoteFrames", &_jumpCoyoteFrames, 1.0f, 0, 10);
    ImGui::DragInt("_jumpInputBufferFrames", &_jumpInputBufferFrames, 1.0f, 0, 10);
    ImGui::DragFloat("_landingApplyMassMult", &_landingApplyMassMult);

    ImGui::Separator();

    ImGui::Text(("_isWeaponDrawn: " + std::to_string(_isWeaponDrawn)).c_str());
    ImGui::Text(("_attackStage: " + std::to_string((int32_t)_attackStage)).c_str());
    ImGui::DragFloat("_spinAttackUpwardsSpeed", &_spinAttackUpwardsSpeed);
    ImGui::DragInt("_weaponCollisionProps.numRays", (int*)&_weaponCollisionProps.numRays, 1.0f, 0, 10);
    ImGui::DragFloat("_weaponCollisionProps.startOffset", &_weaponCollisionProps.startOffset);
    ImGui::DragFloat("_weaponCollisionProps.distance", &_weaponCollisionProps.distance);
    
    ImGui::Separator();

    ImGui::Text(("_airDashMove: " + std::to_string(_airDashMove)).c_str());
    ImGui::Text(("_usedAirDash: " + std::to_string(_usedAirDash)).c_str());
    ImGui::DragFloat3("_airDashDirection", &_airDashDirection[0]);
    ImGui::DragFloat("_airDashTime", &_airDashTime);
    ImGui::DragFloat("_airDashTimeElapsed", &_airDashTimeElapsed);
    ImGui::DragFloat("_airDashSpeed", &_airDashSpeed);
    ImGui::DragFloat("_airDashFinishSpeedFracCooked", &_airDashFinishSpeedFracCooked);
    ImGui::DragFloat("_airDashFinishSpeedFrac", &_airDashFinishSpeedFrac);
}

void Player::processGrounded(glm::vec3& velocity, float_t& groundAccelMult, const float_t& physicsDeltaTime)
{
    // Clear state
    glm::vec3 attachmentVelocityReset(0);
    _isAttachedBodyStale = true;

    // Check if on ground
    if (_jumpPreventOnGroundCheckFramesTimer < 0)
    {
        const float_t targetLength = _adjustedHalfHeight + _bottomRaycastFeetDist;
        const float_t rayLength = targetLength + _bottomRaycastExtraDist;
        auto bodyPos = _physicsObj->body->getWorldTransform().getOrigin();
        auto hitInfo = PhysicsEngine::getInstance().raycast(bodyPos, bodyPos + btVector3(0, -rayLength, 0));
        PhysicsEngine::getInstance().debugDrawLineOneFrame(                                 physutil::toVec3(bodyPos),   physutil::toVec3(bodyPos + btVector3(0, -targetLength, 0)),   glm::vec3(1, 1, 0));
        PhysicsEngine::getInstance().debugDrawLineOneFrame(physutil::toVec3(bodyPos + btVector3(0, -targetLength, 0)),      physutil::toVec3(bodyPos + btVector3(0, -rayLength, 0)),   glm::vec3(1, 0, 0));
        if (hitInfo.hasHit())
        {
            bool isOnFlatGround = hitInfo.m_hitNormalWorld.y() > glm::cos(glm::radians(47.0f));
            if (isOnFlatGround)
            {
                // See if on ground (raycast hit generally flat ground)
                if (_stepsSinceLastGrounded <= 1)  // @NOTE: Only snap to the ground if the previous step was a real _onGround situation
                    _onGround = true;
                else if (hitInfo.m_closestHitFraction * rayLength <= targetLength)
                    _onGround = true;

                if (_onGround)  // Correct the distance from ground and this floating body if "_onGround"
                {
                    float_t targetLengthDifference = targetLength - hitInfo.m_closestHitFraction * rayLength;
                    _displacementToTarget.y = targetLengthDifference;  // Move up even though raycast was down bc we want to go the opposite direction the raycast went.

                    if (hitInfo.m_collisionObject->getInternalType() & btCollisionObject::CO_RIGID_BODY)
                    {
                        //
                        // Send message to ground below the mass of this raycast
                        // (i.e. pretend that the raycast is the body and it has mass)
                        //
                        auto otherBody = (btRigidBody*)hitInfo.m_collisionObject;
                        otherBody->activate();

                        bool firstInteraction = (_framesSinceAttachedBody > 1 || _attachedBody != otherBody);

                        btVector3 force(
                            0,
                            (velocity.y + PhysicsEngine::getInstance().getGravity().y()) * _physicsObj->body->getMass(),
                            0
                        );
                        if (firstInteraction)
                            force.setY(velocity.y * _physicsObj->body->getMass() * _landingApplyMassMult);

                        btVector3 relPos = hitInfo.m_hitPointWorld - otherBody->getWorldTransform().getOrigin();
                        otherBody->applyForce(force, relPos);

                        //
                        // Process moving platform information
                        //
                        if (otherBody->getMass() >= _physicsObj->body->getMass() && !firstInteraction)
                        {
                            // Find delta of moving platform
                            attachmentVelocityReset += physutil::toVec3(otherBody->getWorldTransform() * physutil::toVec3(_attachmentLocalPosition) - physutil::toVec3(_attachmentWorldPosition));
                        }

                        // Setup/keep moving the attachment
                        // @NOTE: this data is used for jump pushaway too
                        _attachedBody = otherBody;
                        _isAttachedBodyStale = false;
                        _framesSinceAttachedBody = 0;

                        auto awp = hitInfo.m_hitPointWorld;
                        auto alp = otherBody->getWorldTransform().inverse() * awp;
                        _attachmentWorldPosition = physutil::toVec3(awp);
                        _attachmentLocalPosition = physutil::toVec3(alp);
                    }

                    // Try to get physics stats from physics object
                    if (Entity* ent = _em->getEntityViaGUID(*(std::string*)hitInfo.m_collisionObject->getUserPointer()); ent != nullptr)
                        if (Yosemite* yos = dynamic_cast<Yosemite*>(ent); yos != nullptr)
                        {
                            attachmentVelocityReset += yos->getTreadmillVelocity() * physicsDeltaTime;
                            groundAccelMult = yos->getGroundedAccelMult();
                        }
                }
            }
            else
            {
                // See if hit ray length is <=targetLength (to enact displacement)
                bool enactDisplacement = false;
                if (hitInfo.m_closestHitFraction * rayLength <= targetLength)  // @COPYPASTA
                    enactDisplacement = true;

                if (enactDisplacement)  // Correct if the knee space ray is hitting the ground underneath while on a steep slope
                {
                    glm::vec3 hitNormalWorld = physutil::toVec3(hitInfo.m_hitNormalWorld);
                    float_t targetLengthDifference = targetLength - hitInfo.m_closestHitFraction * rayLength;
                    float_t UdotN = glm::dot(hitNormalWorld, glm::vec3(0, 1, 0));
                    _displacementToTarget = hitNormalWorld * targetLengthDifference * UdotN;

                    // Additional displacement to make sure player doesn't push into the slope (using velocity)
                    glm::vec3 flatVelocity = glm::vec3(velocity.x, 0, velocity.z);
                    if (glm::length2(flatVelocity) > 0.0001f)
                    {
                        glm::vec3 flatHitNormalWorldNormalized = glm::normalize(glm::vec3(hitNormalWorld.x, 0, hitNormalWorld.z));
                        float_t NVdotNN = glm::dot(glm::normalize(flatVelocity), flatHitNormalWorldNormalized);
                        if (NVdotNN < 0.0f)
                        {
                            const float_t extraDisplacementStrength = -NVdotNN;
                            // _displacementToTarget += flatVelocity * physicsDeltaTime * extraDisplacementStrength;
                            _displacementToTarget += flatHitNormalWorldNormalized * glm::length(flatVelocity) * physicsDeltaTime * extraDisplacementStrength;
                        }
                    }
                }
            }
        }

        // Fire rays downwards in circular pattern to find approx what direction to displace
        // @NOTE: only if the player is falling and ground is inside the faked "knee space"
        //
        // @THOUGHTS: @MAYBE: try this out @@TODO
        //                    Instead of these being supplementary checks to push the player away, what is these were simply onground checkers like the main downcast?
        //                    And then we don't have to do any displacing. It would make the bottom of the collider like a cylinder, bc these supplementary main downcasts
        //                    should end at the same Y position as the main one, and should also have the extra distance to find stairs underneath and whatnot.
        //                    Now, these only fire if the main downcast fails, and this would run on the average hit normal and the average spot that it collided. This way
        //                    we can have an animation where it's like 'ooohhh I'm on the edge' too. If going up stairs or slopes, bc the main downcast will shortcircuit if
        //                    it suceeds, then it still has the appearance of being the point at which stuff happens, but it should also be able to happen at any of these
        //                    16 spots too.
        //                      -Timo 2022/11/09
        //            @PS: I don't really think it'd work extremely well if the radius of the capsule collider were so small that a thin piece of geometry would be able to slide between raycasts however.
        if (!hitInfo.hasHit() && !_onGround && velocity.y < 0.0f)
        {
            constexpr uint32_t numSamples = 16;
            constexpr float_t circularPatternAngleFromOrigin = glm::radians(47.0f);
            constexpr glm::vec3 rotationEulerIncrement = glm::vec3(0, glm::radians(360.0f) / (float_t)numSamples, 0);
            const glm::quat rotatorQuaternion(rotationEulerIncrement);

            glm::vec3 circularPatternOffset = glm::vec3(0.0f, -glm::sin(circularPatternAngleFromOrigin), 1.0f) * _capsuleRadius;
            glm::vec3 accumulatedHitPositions(0.0f),
                      averageHitNormals(0.0f);

            const glm::vec3 bodyFootSuckedInPosition = physutil::toVec3(bodyPos - btVector3(0, targetLength - _capsuleRadius, 0));

            for (uint32_t i = 0; i < numSamples; i++)
            {
                // Draw debug
                glm::vec3 circularPatternR0 = physutil::toVec3(bodyPos) + circularPatternOffset;
                glm::vec3 circularPatternR1 = bodyFootSuckedInPosition + circularPatternOffset;
                PhysicsEngine::getInstance().debugDrawLineOneFrame(circularPatternR0, circularPatternR1, glm::vec3(1, 0.5, 0.75));
                auto hitInfo = PhysicsEngine::getInstance().raycast(physutil::toVec3(circularPatternR0), physutil::toVec3(circularPatternR1));
                if (hitInfo.hasHit())
                {
                    accumulatedHitPositions += physutil::toVec3(hitInfo.m_hitPointWorld) - bodyFootSuckedInPosition;
                    averageHitNormals       += physutil::toVec3(hitInfo.m_hitNormalWorld);
                }

                // Increment circular pattern
                circularPatternOffset = rotatorQuaternion * circularPatternOffset;
            }

            // Normalize the accumulatedHitPositions
            accumulatedHitPositions.y = 0.0f;
            if (glm::length2(accumulatedHitPositions) > 0.0001f)
            {
                averageHitNormals = glm::normalize(averageHitNormals);
                bool isOnFlatGround = averageHitNormals.y > glm::cos(glm::radians(47.0f));
                if (isOnFlatGround)
                {
                    glm::vec3 pushAwayDirection = -glm::normalize(accumulatedHitPositions);

                    float_t pushAwayForce = 1.0f;
                    if (glm::length2(_worldSpaceInput) > 0.0001f)
                        pushAwayForce = glm::clamp(glm::dot(pushAwayDirection, glm::normalize(_worldSpaceInput)), 0.0f, 1.0f);  // If you're pushing the stick towards the ledge like to climb up it, then you should be able to do that with the knee-space

                    // @HEURISTIC: I dont think this is the "end all be all solution" to this problem
                    //             but I do think it is the "end all be all soltuion" for this game
                    //             (and then have the next step see if needs to increment more)
                    //                 -Timo
                    const float_t displacementMagnitude = (1.0f - glm::cos(circularPatternAngleFromOrigin)) * _capsuleRadius;
                    glm::vec3 flatDisplacement = pushAwayDirection * pushAwayForce * displacementMagnitude;
                    _displacementToTarget.x += flatDisplacement.x;
                    _displacementToTarget.z += flatDisplacement.z;
                }
                else
                {
                    // @COPYPASTA
                    // Additional displacement to make sure player doesn't push into the slope (using velocity)
                    glm::vec3 flatVelocity = glm::vec3(velocity.x, 0, velocity.z);
                    if (glm::length2(flatVelocity) > 0.0001f)
                    {
                        glm::vec3 flatHitNormalWorldNormalized = glm::normalize(glm::vec3(averageHitNormals.x, 0, averageHitNormals.z));
                        float_t NVdotNN = glm::dot(glm::normalize(flatVelocity), flatHitNormalWorldNormalized);
                        if (NVdotNN < 0.0f)
                        {
                            const float_t extraDisplacementStrength = -NVdotNN;
                            _displacementToTarget += flatHitNormalWorldNormalized * glm::length(flatVelocity) * physicsDeltaTime * extraDisplacementStrength;
                        }
                    }
                }
            }
        }
    }
    else
        _jumpPreventOnGroundCheckFramesTimer--;

    // Process if grounded or not
    if (_onGround)
    {
        if (_stepsSinceLastGrounded > 8)  // @NOTE: @HARDCODED just a random number
            AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_OOT_Steps_Dirt1.wav",
                "res/sfx/wip_OOT_Steps_Dirt2.wav",
                "res/sfx/wip_OOT_Steps_Dirt3.wav",
                "res/sfx/wip_OOT_Steps_Dirt4.wav",
                });
        _stepsSinceLastGrounded = 0;
        _usedAirDash = false;
        _usedSpinAttack = false;
        _physicsObj->body->setGravity(btVector3(0, 0, 0));
        velocity.y = 0.0f;
    }
    else
    {
        _physicsObj->body->setGravity(PhysicsEngine::getInstance().getGravity());

        if (_stepsSinceLastGrounded)
            _characterRenderObj->animator->setTrigger("goto_fall");

        // Retain velocity from _prevAttachmentVelocity if just leaving the ground
        if (glm::length2(attachmentVelocityReset) < 0.0001f)
        {
            // std::cout << "\tCLEARREQ: " << _prevAttachmentVelocity.x<<","<<_prevAttachmentVelocity.y<<","<<_prevAttachmentVelocity.z<<std::endl;
            glm::vec3 applyVelocity =
                glm::vec3(
                    _prevAttachmentVelocity.x,
                    glm::max(0.0f, _prevAttachmentVelocity.y),
                    _prevAttachmentVelocity.z
                ) / physicsDeltaTime;
            velocity += applyVelocity;
        }
    }

    // Clear attachment velocity
    _prevAttachmentVelocity = _attachmentVelocity;
    _attachmentVelocity     = attachmentVelocityReset;
}

void Player::startAttack(AttackType type)
{
    switch (type)
    {
    case AttackType::HORIZONTAL:
        _characterRenderObj->animator->setTrigger("goto_horizontal_attack_prepause");
        break;

    case AttackType::DIVE_ATTACK:
        _characterRenderObj->animator->setTrigger("goto_dive_attack_prepause");
        break;
    }

    _attackStage = AttackStage::PREPAUSE;
    _attackType = type;
    _attackPrepauseTimeElapsed = 0.0f;
}

void Player::processAttackStageSwing(glm::vec3& velocity, const float_t& physicsDeltaTime)
{
    //
    // Process attack combo behavior
    //
    if (_attackSwingTimeElapsed == 0.0f)
    {
        // Reset the attack stage state
        _allowComboInput      = false;
        _allowComboTransition = false;
        _flagAttack = false;
    }
    else if (!_allowComboInput)
        _flagAttack = false;   // Prevent attack input from happening except when combo input window is open
    else if (_allowComboTransition && _flagAttack)
    {
        // Transition to next combo!
        // @COPYPASTA        
        _flagAttack = false;
        _weaponPrevTransform = glm::mat4(0);
        _attackSwingTimeElapsed = 0.0f;
        _characterRenderObj->animator->setTrigger("goto_next_combo_attack");
        return;
    }

    //
    // Actually process the attack swing stage
    //
    switch (_attackType)
    {
    case AttackType::HORIZONTAL:
        // All taken care of by the state machine
        break;

    case AttackType::DIVE_ATTACK:
    {
        glm::vec3 diveDirection = glm::vec3(0, -glm::sin(glm::radians(45.0f)), glm::cos(glm::radians(45.0f)));
        velocity = glm::quat(glm::vec3(0, _facingDirection, 0)) * diveDirection * _airDashSpeed;

        // Signal to dive when to end
        if (_onGround)
            _characterRenderObj->animator->setTrigger("goto_dive_attack_end");
    } break;

    case AttackType::SPIN_ATTACK:
    {
        // Ending is taken care of by state machine
        if (_attackSwingTimeElapsed == 0.0f)
        {
            AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_OOT_YoungLink_DinsFire.wav",
                });

            _jumpPreventOnGroundCheckFramesTimer = _jumpPreventOnGroundCheckFrames;
        }

        _usedSpinAttack = true;  // Constant flag setting until we're done (for esp. starting spin attack on ground... it always resets the _usedSpinAttack flag so this is to make sure it doesn't get unset during the duration of the spin attack)
        velocity = glm::vec3(0, _spinAttackUpwardsSpeed, 0);
    } break;
    }

    _attackSwingTimeElapsed += physicsDeltaTime;
}

void Player::processWeaponCollision()
{
    //
    // Calculate approx of weapon collision
    //
    if (_weaponPrevTransform == glm::mat4(0))
    {
        // First frame is wasted, but don't want to leak the last frame into the collision
        _weaponPrevTransform = _weaponRenderObj->transformMatrix;
    }
    else
    {
        float_t cur = _weaponCollisionProps.startOffset;
        float_t step = _weaponCollisionProps.distance / (float_t)_weaponCollisionProps.numRays;
        for (size_t i = 0; i < _weaponCollisionProps.numRays; i++, cur += step)
        {
            glm::vec3 raycastPosition[2] = {
                _weaponPrevTransform              * glm::vec4(0, cur, 0, 1),
                _weaponRenderObj->transformMatrix * glm::vec4(0, cur, 0, 1),
            };

            if (glm::length2(raycastPosition[1] - raycastPosition[0]) > 0.0001f)
            {
                auto hitInfo = PhysicsEngine::getInstance().raycast(physutil::toVec3(raycastPosition[0]), physutil::toVec3(raycastPosition[1]));
                PhysicsEngine::getInstance().debugDrawLineOneFrame(raycastPosition[0], raycastPosition[1], glm::vec3(0, 0, 1));
                if (hitInfo.hasHit())
                {
                    auto& collisionObj = hitInfo.m_collisionObject;

                    std::cout << "[PLAYER ATTACK HIT]" << std::endl
                        << "Detected object guid: " << *(std::string*)collisionObj->getUserPointer() << std::endl;

                    glm::vec3 pushDirection = glm::quat(glm::vec3(0, _facingDirection, 0)) * glm::vec3(0, 0, 1);

                    DataSerializer ds;
                    ds.dumpString("event_attacked");
                    ds.dumpVec3(pushDirection);

                    DataSerialized dsd = ds.getSerializedData();
                    if (_em->sendMessage(*(std::string*)collisionObj->getUserPointer(), dsd))
                    {
                        // velocity = -pushDirection * 10.0f;
                    }
                }
            }
        }

        _weaponPrevTransform = _weaponRenderObj->transformMatrix;
    }
}

void Player::onCollisionStay(btPersistentManifold* manifold, bool amIB)
{
    // @NOTE: this was causing lots of movement popping
    /*_groundContactNormal = glm::vec3(0.0f);
    size_t numContacts = (size_t)manifold->getNumContacts();
    for (int32_t i = 0; i < numContacts; i++)
    {
        auto contact = manifold->getContactPoint(i);
        auto contactNormal = contact.m_normalWorldOnB * (amIB ? -1.0f : 1.0f);
        bool isGroundContactNormal = contactNormal.y() > glm::cos(glm::radians(47.0f));
        if (isGroundContactNormal)
        {
            _onGround = true;
            _groundContactNormal += physutil::toVec3(contactNormal);
        }
    }
    
    if (_onGround)
        _groundContactNormal = glm::normalize(_groundContactNormal);*/
}
