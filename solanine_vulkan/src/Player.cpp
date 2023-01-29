#include "Player.h"

#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "RenderObject.h"
#include "EntityManager.h"
#include "WindManager.h"
#include "Camera.h"
#include "InputManager.h"
#include "AudioEngine.h"
#include "DataSerialization.h"
#include "GlobalState.h"
#include "Debug.h"
#include "imgui/imgui.h"
#include "Yosemite.h"
#include "Scollision.h"


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
                _flagAttack = false;  // To prevent unusual behavior (i.e. had a random attack just start from the beginning despite no inputs. So this is just to make sure)
            }
        },
        {
            "EventGotoNoneAttackStage", [&]() {
                _attackStage = AttackStage::NONE;
                _flagAttack = false;  // To prevent unusual behavior (i.e. had a random attack just start from the beginning despite no inputs. So this is just to make sure)
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

    _totalHeight = 4.5f;
    _maxClimbAngle = glm::radians(47.0f);

    _capsuleRadius = 1.0f;
    //float_t d = (r - r * glm::sin(_maxClimbAngle)) / glm::sin(_maxClimbAngle);  // This is the "perfect algorithm", but we want stair stepping abilities too...
    constexpr float_t raycastMargin = 0.05f;
    _bottomRaycastFeetDist = 2.0f + raycastMargin;
    _bottomRaycastExtraDist = 1.0f + raycastMargin;

    _collisionShape = new btCapsuleShape(_capsuleRadius, _totalHeight - _bottomRaycastFeetDist);  // @NOTE: it appears that this shape has a margin in the direction of the sausage (i.e. Y in this case) and then the radius is the actual radius
    _adjustedHalfHeight = (_totalHeight - _bottomRaycastFeetDist) * 0.5f + _collisionShape->getMargin();

    const glm::vec3 toff(0, -4.25f, 0);
    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            1.0f,
            _load_position - toff,
            glm::quat(glm::vec3(0.0f)),
            _collisionShape,
            &getGUID()
        );
    _physicsObj->transformOffset = toff;
    auto body = _physicsObj->body;
    body->setAngularFactor(0.0f);
    body->setDamping(0.0f, 0.0f);
    body->setFriction(0.0f);
    body->setActivationState(DISABLE_DEACTIVATION);

    // https://docs.panda3d.org/1.10/python/programming/physics/bullet/ccd
    body->setCcdMotionThreshold(1e-7f);
    body->setCcdSweptSphereRadius(0.5f);

    /* // @NOTE: Generated from a gltf file export from blender fyi
    btCompoundShape* bcs = new btCompoundShape();
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -2.427243232727051,
                4.324337005615234,
                -0.7442868947982788
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -4.652688980102539,
                4.324337005615234,
                -1.7126106023788452
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -4.754617691040039,
                4.324337005615234,
                0.05415546894073486
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -4.414855003356934,
                4.324337005615234,
                1.8379096984863281
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -1.6967535018920898,
                4.324337005615234,
                1.6170639991760254
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -2.206397533416748,
                4.324337005615234,
                0.46187078952789307
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -3.718341588973999,
                4.324337005615234,
                -1.067061424255371
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -3.2766499519348145,
                4.324337005615234,
                1.2433249950408936
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                0.07001256942749023,
                4.324337005615234,
                3.8594977855682373
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                0.3588108718395233,
                4.324337005615234,
                2.5004467964172363
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -0.3377026617527008,
                4.324337005615234,
                5.286500930786133
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -1.7816940546035767,
                4.324337005615234,
                4.776856899261475
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -3.46351957321167,
                4.324337005615234,
                3.4857585430145264
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -2.223385810852051,
                4.324337005615234,
                2.993102550506592
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                -0.8303587436676025,
                4.324337005615234,
                2.976114511489868
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                1.8197903633117676,
                4.324337005615234,
                2.2965891361236572
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                2.7541377544403076,
                4.324337005615234,
                3.553711175918579
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                3.7564377784729004,
                4.324337005615234,
                4.793845176696777
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                1.9896717071533203,
                4.324337005615234,
                5.490358829498291
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                1.3611106872558594,
                4.324337005615234,
                4.301188945770264
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                5.217417240142822,
                4.324337005615234,
                2.5514111518859863
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                2.8220901489257812,
                4.324337005615234,
                1.2942891120910645
            )
        ),
        new btSphereShape(1.0f)
    );
    bcs->addChildShape(
        btTransform(
            btQuaternion(0, 0, 0, 1),
            btVector3(
                3.977283477783203,
                4.324337005615234,
                2.0077908039093018
            )
        ),
        new btSphereShape(1.0f)
    );*/

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
        }

        if (_isWeaponDrawn && (_attackStage >= AttackStage::SWING || (_attackStage == AttackStage::PREPAUSE && !_attackPrepauseReady)))
        {
            input = glm::vec2(0);
            _flagJump = false;
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

    velocity -= (_displacementToTarget + _attachmentVelocity) / physicsDeltaTime + _windZoneVelocity;  // Undo the displacement (hopefully no movement bugs)
    _displacementToTarget = glm::vec3(0.0f);
    _windZoneVelocity = glm::vec3(0.0f);
    // _attachmentVelocity   = glm::vec3(0.0f);

    float groundAccelMult;
    processGrounded(velocity, groundAccelMult, physicsDeltaTime);

    //
    // Process combat mode
    //
    switch (_attackStage)
    {
    case AttackStage::NONE:
    {
        if (_flagAttack)  // @TODO: ct
        {
            _flagAttack = false;
            startAttack(
                _onGround ? AttackType::HORIZONTAL : AttackType::DIVE_ATTACK  // @NOTE: weapon is already drawn at this point
            );
        }
        break;
    }

    case AttackStage::PREPAUSE:
    {
        if (!_attackPrepauseReady)
        {
            if (_attackPrepauseTimeElapsed > _attackPrepauseTime)
            {
                // Move to prepause ready state
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_OOT_Sword_Away_Edited.wav",
                });
                _characterRenderObj->animator->setTrigger(
                    _onGround ?
                    "goto_finish_prepause_grounded" :
                    "goto_finish_prepause_airborne"
                );
                _characterRenderObj->animator->setTrigger(
                    "goto_mcm_grounded_prepause_ready"
                );

                _attackPrepauseReady = true;
            }

            _attackPrepauseTimeElapsed += physicsDeltaTime;
        }

        if (_attackType == AttackType::DIVE_ATTACK)
        {
            // Suspend self in the air until attack prepause is ready
            if (!_attackPrepauseReady)
                velocity = glm::vec3(0);
            _physicsObj->body->setGravity(_attackPrepauseReady ? PhysicsEngine::getInstance().getGravity() : btVector3(0, 0, 0));  // Make sure this isn't over imposing since it's setting the gravity every frame (note: could overwrite some other code's work when changing the gravity)
        }

        if (_attackPrepauseReady && _flagAttack)
        {
            // Move onto swing attack
            _attackType  = (_onGround ? AttackType::HORIZONTAL : AttackType::DIVE_ATTACK);  // @COPYPASTA
            _characterRenderObj->animator->setTrigger(
                _attackType == AttackType::HORIZONTAL ? 
                "goto_combat_execute_grounded" :
                "goto_combat_execute_airborne"
            );
            _characterRenderObj->animator->setTrigger("leave_mcm_grounded_prepause_ready");
            _attackStage = AttackStage::SWING;
            _attackSwingTimeElapsed = 0.0f;
        }

        // Reset flags in preparation for next step
        _flagAttack = false;
        _weaponPrevTransform = glm::mat4(0);

        /* @TODO: Idk if the spin attack should be a part of the attack mvt set... so just comment it out for now eh!
        if (!_usedSpinAttack && _flagJump)  // Check jump flag before flag gets nuked
        {
            // Switch attack type to spin attack
            _attackStage = AttackStage::SWING;
            _attackType = AttackType::SPIN_ATTACK;
            _attackSwingTimeElapsed = 0.0f;
        }
        */

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

    //
    // Process air dash
    //
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
    if (_beingGrabbedData.stage)
    {
        //
        // Being Grabbed method (go to the exact position that's requested)
        // @TODO: see if you can just turn off gravity of the rigidbody when it's being grabbed
        //
        if (_beingGrabbedData.stage == 1)
        {
            // Being grabbed
            btVector3 pos   = _physicsObj->body->getWorldTransform().getOrigin();
            btVector3 delta = physutil::toVec3(_beingGrabbedData.gotoPosition) - pos;
            _facingDirection = _beingGrabbedData.gotoFacingDirection;

            _physicsObj->body->setLinearVelocity(delta / physicsDeltaTime);
        }
        else if (_beingGrabbedData.stage == 2)
        {
            // @BUG: sometimes the kickout is weaker in the Y axis. It seems like it's caused by the enemy capsule collider shape rubbing up against the player collider shape for some reason. Verified cause of the weak kickout is unknown however
            // @FOLLOWUP: It seems like lagging (<30fps) will cause a weaker Y axis kickout.

            // Being kicked out
            _onGround = false;  // To force going to a fall state when getting kicked out (NOTE this doesn't eliminate the times that the player gets not as high of a push up, just reduce it to very rare cases. So this fix does something!  -Timo 2023/01/26)
            _stepsSinceLastGrounded = _jumpCoyoteFrames;  // This is to prevent ground sticking right after a jump and multiple jumps performed right after another jump was done!

            std::cout << ".stage = 0" << std::endl;
            _beingGrabbedData.stage = 0;

            std::cout << "KV:  " << _beingGrabbedData.kickoutVelocity.x << ", " << _beingGrabbedData.kickoutVelocity.y << ", "  << _beingGrabbedData.kickoutVelocity.z << std::endl;
            _physicsObj->body->setLinearVelocity(physutil::toVec3(_beingGrabbedData.kickoutVelocity));
        }
    }
    else
    {
        //
        // Normal Method (based off input movement)
        //
        glm::vec3 desiredVelocity = _worldSpaceInput * _maxSpeed;  // @NOTE: we just ignore the y component in this desired velocity thing

        glm::vec2 a(velocity.x, velocity.z);
        glm::vec2 b(desiredVelocity.x, desiredVelocity.z);

        if (_onGround)
        {
            // Grounded Movement animation processing
            GroundedMovementStage newStage;
            if (glm::length2(b) < 0.0001f)
                newStage = GroundedMovementStage::IDLE;
            else
                newStage = GroundedMovementStage::RUN;

            // Trigger animator with new animations
            if (newStage != _groundedMovementStage)
            {
                _groundedMovementStage = newStage;
                switch (newStage)
                {
                    case GroundedMovementStage::IDLE:
                    {
                        _characterRenderObj->animator->setTrigger("goto_idle");
                    } break;

                    case GroundedMovementStage::RUN:
                    {
                        _characterRenderObj->animator->setTrigger("goto_run");
                    } break;

                    case GroundedMovementStage::RUN_REALLY_FAST:  break;
                    case GroundedMovementStage::GROUNDED_DASH:  break;
                    case GroundedMovementStage::SKID_TO_HALT:  break;
                    case GroundedMovementStage::TURN_AROUND:  break;
                }
            }
        }
        else
            _groundedMovementStage = GroundedMovementStage::NONE;

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

        if (_flagJump)
        {
            //
            // Sheath the weapon  @COPYPASTA
            //
            if (_isWeaponDrawn)
            {
                if (_attackStage == AttackStage::PREPAUSE && _attackPrepauseReady && !_onGround)
                    _characterRenderObj->animator->setTrigger("goto_fall");

                // Reset attack state
                _attackStage = AttackStage::NONE;
                _attackPrepauseTimeElapsed = 0.0f;
                _attackPrepauseReady = false;

                // Sheath the weapon
                AudioEngine::getInstance().playSoundFromList({
                    // "res/sfx/wip_sheath_weapon.ogg",
                    // "res/sfx/wip_sheath_weapon_2.ogg",
                    "res/sfx/wip_poweroff1.wav",
                    "res/sfx/wip_poweroff2.wav",
                    "res/sfx/wip_poweroff3.wav",
                });

                _weaponRenderObj->renderLayer = RenderLayer::INVISIBLE;
                _weaponAttachmentJointName = "Back Attachment";
                _isWeaponDrawn = false;
            }

            //
            // Do the normal jump
            //
            enum JumpType
            {
                GROUNDED_JUMP,
                AIR_DASH,
                FAILED_AIR_DASH,
            } jumpType;

            jumpType = (_onGround || (int32_t)_stepsSinceLastGrounded <= _jumpCoyoteFrames) ? GROUNDED_JUMP : (_usedAirDash ? FAILED_AIR_DASH : AIR_DASH);

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
                        "res/sfx/wip_jump2.ogg",
                        //"res/sfx/wip_hollow_knight_sfx/hero_jump.wav",
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

            case FAILED_AIR_DASH:
                _flagJump = false;  // @NOTE: this is an unsuccessful jump input. It got canceled out bc there were no more airdashes left
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

        btVector3 linVelo = physutil::toVec3(velocity + (_displacementToTarget + _attachmentVelocity) / physicsDeltaTime + _windZoneVelocity);
        _physicsObj->body->setLinearVelocity(linVelo);
    }
}

void Player::update(const float_t& deltaTime)
{
    _attackedDebounceTimer -= deltaTime;
    
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

    if (input::onLMBPress)
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
        {
            input = glm::vec2(0.0f);
            _flagJump = false;
            _flagAttack = false;
        }

        if (_beingGrabbedData.stage == 1)
        {
            input = glm::vec2(0);
            _flagJump = false;
            _flagAttack = false;
        }

        if (_isWeaponDrawn && (_attackStage >= AttackStage::SWING || (_attackStage == AttackStage::PREPAUSE && !_attackPrepauseReady)))
        {
            input = glm::vec2(0);
            _flagJump = false;
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

    //
    // Update mask for animation
    // @TODO: there is popping for some reason. Could be how the transitions/triggers work in the game or could be a different underlying issue. Figure it out pls!  -Timo
    //
    _characterRenderObj->animator->setMask(
        "MaskCombatMode",
        _isWeaponDrawn && (_attackStage == AttackStage::NONE || (_attackStage == AttackStage::PREPAUSE && _attackPrepauseReady && _onGround))
    );
}

void Player::lateUpdate(const float_t& deltaTime)
{
    //
    // Update position of character and weapon
    //
    glm::vec3 interpPos                  = physutil::getPosition(_physicsObj->interpolatedTransform);
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
    auto eventName = message.loadString();
    if (eventName == "event_attacked")
    {
        if (_attackedDebounceTimer > 0.0f)
            return false;

        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_OOT_YoungLink_Hurt1.wav",
            "res/sfx/wip_OOT_YoungLink_Hurt2.wav",
            "res/sfx/wip_OOT_YoungLink_Hurt3.wav",
        });

        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_punch_sound.ogg",
        });

        globalState::savedPlayerHealth--;

        _attackedDebounceTimer = _attackedDebounce;

        return true;
    }
    else if (eventName == "event_grapple_hold")
    {
        // Cancel out early if the message is blocked with an attack stage of swing
        if (_isWeaponDrawn && _attackStage == AttackStage::SWING)  return false;

        // @TODO: add some animation that cancels out the attack mode
        if (_beingGrabbedData.stage == 0)
        {
            _characterRenderObj->animator->setTrigger("goto_grabbed");
        }


        // @TODO: add some animation that does the grapple kickout event
        // @TODO: add some animation that does the grapple release (just a quick couple frame thing of bursting out motion that then reverts back to idle state)

        // Undo inputs
        _flagJump   = false;
        _flagAttack = false;
        
        // Sheath the weapon (@COPYPASTA)
        if (_isWeaponDrawn)
        {
            // Reset attack state
            _attackStage = AttackStage::NONE;
            _attackPrepauseTimeElapsed = 0.0f;
            _attackPrepauseReady = false;

            // Sheath the weapon
            AudioEngine::getInstance().playSoundFromList({
                // "res/sfx/wip_sheath_weapon.ogg",
                // "res/sfx/wip_sheath_weapon_2.ogg",
                "res/sfx/wip_poweroff1.wav",
                "res/sfx/wip_poweroff2.wav",
                "res/sfx/wip_poweroff3.wav",
            });

            _weaponRenderObj->renderLayer = RenderLayer::INVISIBLE;
            _weaponAttachmentJointName = "Back Attachment";
            _isWeaponDrawn = false;
        }

        // Process grapple event
        glm::vec3 grapplePoint         = message.loadVec3();
        float_t   forceFacingDirection = message.loadFloat();

        std::cout << ".stage = 1" << std::endl;
        _beingGrabbedData.stage               = 1;
        _beingGrabbedData.gotoPosition        = grapplePoint;
        _beingGrabbedData.gotoFacingDirection = forceFacingDirection;

        _physicsObj->body->setGravity(btVector3(0, 0, 0));

        return true;
    }
    else if (eventName == "event_grapple_release")
    {
        if (_beingGrabbedData.stage == 0)  return false;  // @NOTE: Upon successful hit on an enemy that's trying to request a grab on you, it will emit this event. Ignore it if you never accepted the grab in the first place!

        _characterRenderObj->animator->setTrigger("leave_grabbed");
        if (_isWeaponDrawn)
        {
            _attackStage = AttackStage::NONE;  // For preventing input lockups when the attackstage never gets reset from the animation event
        }

        std::cout << ".stage = 0" << std::endl;
        _beingGrabbedData.stage = 0;

        return true;
    }
    else if (eventName == "event_grapple_kickout")
    {
        if (_beingGrabbedData.stage == 0)  return false;  // @NOTE: Upon successful hit on an enemy that's trying to request a grab on you, it will emit this event. Ignore it if you never accepted the grab in the first place!

        _characterRenderObj->animator->setTrigger("leave_grabbed");
        if (_isWeaponDrawn)
        {
            _attackStage = AttackStage::NONE;  // For preventing input lockups when the attackstage never gets reset from the animation event
        }

        glm::vec3 launchVelocity = message.loadVec3();
        std::cout << "LV:  " << launchVelocity.x << ", " << launchVelocity.y << ", "  << launchVelocity.z << std::endl;

        std::cout << ".stage = 2" << std::endl;
        _beingGrabbedData.stage = 2;
        _beingGrabbedData.kickoutVelocity = launchVelocity;

        _physicsObj->body->setGravity(PhysicsEngine::getInstance().getGravity());

        return true;
    }

    return false;
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
                        bool firstInteraction = (_framesSinceAttachedBody > 1 || _attachedBody != otherBody);

                        if (otherBody->getMass() != 0.0f)  // Check to make sure that this is a dynamic rigidbody first
                        {
                            otherBody->activate();

                            btVector3 force(
                                0,
                                (velocity.y + PhysicsEngine::getInstance().getGravity().y()) * _physicsObj->body->getMass(),
                                0
                            );
                            if (firstInteraction)
                                force.setY(velocity.y * _physicsObj->body->getMass() * _landingApplyMassMult);

                            btVector3 relPos = hitInfo.m_hitPointWorld - otherBody->getWorldTransform().getOrigin();
                            otherBody->applyForce(force, relPos);
                        }

                        //
                        // Process moving platform information
                        //
                        if ((otherBody->getMass() == 0.0f || otherBody->getMass() >= _physicsObj->body->getMass()) && !firstInteraction)
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
                        else if (Scollision* sco = dynamic_cast<Scollision*>(ent); sco != nullptr)
                        {
                            groundAccelMult = sco->getGroundedAccelMult();
                        }
                        else
                            groundAccelMult = 1.0f;  // Default Value
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
        if (_stepsSinceLastGrounded > 8)  // @NOTE: @HARDCODED just a number
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
            _characterRenderObj->animator->setTrigger(
                _attackPrepauseReady ?
                "goto_fall_combat_prepause_ready" :
                "goto_fall"
            );

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
            velocity += applyVelocity;  // @TODO: fix this bc the velocity that gets applied is way too much! Idk how to adjust this or make sure that velocity is retained. This might be a nasty @BUG
        }
    }

    // Check for wind velocity
    windmgr::WZOState wzo =
        windmgr::getWindZoneOccupancyState(
            physutil::toVec3(_physicsObj->body->getWorldTransform().getOrigin()));
    if (wzo != (windmgr::WZOState)_windZoneOccupancyPrevEnum)
    {
        // Stop any sfx being played
        if (_windZoneSFXChannelId >= 0)
            AudioEngine::getInstance().stopChannel(_windZoneSFXChannelId);
        _windZoneSFXChannelId = -1;

        switch (wzo)
        {
        case windmgr::WZOState::NONE:
        {
        }
        break;
            
        case windmgr::WZOState::INSIDE:
        {
            _windZoneSFXChannelId = AudioEngine::getInstance().playSound("res/sfx/wip_ultimate_wind_noise_generator_90_loop.ogg", true);
        }
        break;
            
        case windmgr::WZOState::INSIDE_OCCLUDED:
        {
            _windZoneSFXChannelId = AudioEngine::getInstance().playSound("res/sfx/wip_ultimate_wind_noise_generator_90_loop_lowpassed.ogg", true);
        }
        break;
        }

        _windZoneOccupancyPrevEnum = (int32_t)wzo;
    }

    if (wzo == windmgr::WZOState::INSIDE)
    {
        // @NOTE: this needs to be set every frame
        float_t windVelocityStrength =
            _onGround ?
                glm::max(0.25f, glm::length(_worldSpaceInput)) :
                1.0f;
        _windZoneVelocity =
            windmgr::windVelocity * windVelocityStrength;
    }

    // Clear attachment velocity
    _prevAttachmentVelocity = _attachmentVelocity;
    _attachmentVelocity     = attachmentVelocityReset;
}

void Player::startAttack(AttackType type)
{
    _attackType = type;

    _characterRenderObj->animator->setTrigger("goto_combat_prepause");
    _attackStage = AttackStage::PREPAUSE;
    _attackPrepauseTimeElapsed = 0.0f;
    _attackPrepauseReady = false;

    if (!_isWeaponDrawn)
    {
        // Draw the weapon
        AudioEngine::getInstance().playSoundFromList({
            // "res/sfx/wip_draw_weapon.ogg",
            "res/sfx/wip_poweron1.wav",
            "res/sfx/wip_poweron2.wav",
            "res/sfx/wip_poweron3.wav",
        });

        _weaponRenderObj->renderLayer = RenderLayer::VISIBLE;
        _weaponAttachmentJointName = "Hand Attachment";
        _isWeaponDrawn = true;
    }
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
    {
        _flagAttack = false;   // Prevent attack input from happening except when combo input window is open
    }
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
    {
        if (_attackSwingTimeElapsed == 0.0f)
        {
            /*AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_OOT_YoungLink_DinsFire.wav",
                //"res/sfx/wip_hollow_knight_sfx/hero_nail_art_cyclone_slash_short.wav",
                });*/

        }

        // If in a wind zone, go upwards with the wing
        if (_windZoneOccupancyPrevEnum == (int32_t)windmgr::WZOState::INSIDE)
        {
            velocity = glm::vec3(0, _spinAttackUpwardsSpeed, 0);

            // Prevent ground sticking every frame you're doing an attack
            _jumpPreventOnGroundCheckFramesTimer = _jumpPreventOnGroundCheckFrames;  // @REGRESSION @BUG: player will fall thru the ground while doing the swing
        }
        else
            velocity = glm::vec3(0, glm::max(0.0f, velocity.y), 0);
    }
    break;

    case AttackType::DIVE_ATTACK:
    {
        glm::vec3 diveDirection = glm::vec3(0, -glm::sin(glm::radians(45.0f)), glm::cos(glm::radians(45.0f)));
        velocity = glm::quat(glm::vec3(0, _facingDirection, 0)) * diveDirection * _airDashSpeed;

        // Signal to dive when to end
        if (_onGround)
            _characterRenderObj->animator->setTrigger("goto_dive_attack_end");
    }
    break;

    case AttackType::SPIN_ATTACK:
    {
        // Ending is taken care of by state machine
        if (_attackSwingTimeElapsed == 0.0f)
        {
            AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_OOT_YoungLink_DinsFire.wav",
                //"res/sfx/wip_hollow_knight_sfx/hero_nail_art_cyclone_slash_short.wav",
                });

        }

        _usedSpinAttack = true;  // Constant flag setting until we're done (for esp. starting spin attack on ground... it always resets the _usedSpinAttack flag so this is to make sure it doesn't get unset during the duration of the spin attack)

        // If in a wind zone, go upwards with the wing
        if (_windZoneOccupancyPrevEnum == (int32_t)windmgr::WZOState::INSIDE)
        {
            velocity = glm::vec3(0, _spinAttackUpwardsSpeed, 0);

            // Prevent ground sticking every frame you're doing an attack
            _jumpPreventOnGroundCheckFramesTimer = _jumpPreventOnGroundCheckFrames;  // @REGRESSION @BUG: player will fall thru the ground while doing the swing
        }
        else
            velocity = glm::vec3(0, glm::max(0.0f, velocity.y), 0);
    }
    break;
    }

    _attackSwingTimeElapsed += physicsDeltaTime;
}

void Player::processWeaponCollision()
{
    //
    // Calculate approx of weapon collision
    //
    // @TODO: @NOTE: this will likely be improved in the future, with proper recording of what the weapon slice positions corresponding to the animation timing so that this is all consistent.  -Timo 2023/01/26
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
                    std::string guid = *(std::string*)collisionObj->getUserPointer();
                    if (guid == getGUID()) continue;

                    std::cout << "[PLAYER ATTACK HIT]" << std::endl
                        << "Detected object guid: " << guid << std::endl;

                    glm::vec3 pushDirection = glm::quat(glm::vec3(0, _facingDirection, 0)) * glm::vec3(0, 0, 1);

                    DataSerializer ds;
                    ds.dumpString("event_attacked");
                    ds.dumpVec3(pushDirection);

                    DataSerialized dsd = ds.getSerializedData();
                    if (_em->sendMessage(guid, dsd))
                    {
                        // velocity = -pushDirection * 10.0f;
                    }
                }
            }
        }

        _weaponPrevTransform = _weaponRenderObj->transformMatrix;
    }
}
