#include "Enemy.h"

#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "RenderObject.h"
#include "Camera.h"
#include "InputManager.h"
#include "AudioEngine.h"
#include "DataSerialization.h"
#include "Debug.h"
#include "imgui/imgui.h"


Enemy::Enemy(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds) : Entity(em, ds), _rom(rom), _camera(camera)
{
    if (ds)
        load(*ds);

    _characterModel = _rom->getModel("EnemyWIP");

    _renderObj =
        _rom->registerRenderObject({
            .model = _characterModel,
            // .animator = new vkglTF::Animator(_characterModel),
            .transformMatrix = glm::translate(glm::mat4(1.0f), _load_position) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0))),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

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
            _collisionShape
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

    _enableUpdate = true;
    _enablePhysicsUpdate = true;
}

Enemy::~Enemy()
{
    //delete _renderObj->animator;
    _rom->unregisterRenderObject(_renderObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    // @TODO: figure out if I need to call `delete _collisionShape;` or not
}

void Enemy::update(const float_t& deltaTime)
{
    //
    // Calculate render object transform
    // @NOTE: this isn't getting controlled by the controls anymore... this will be controlled by an ai in the future for the enemy!!! Just leaving it in as a reminder
    //
    /*if (input::onKeyJumpPress)
    {
        _flagJump = true;
        _jumpInputBufferFramesTimer = _jumpInputBufferFrames;
    }

    if (input::onRMBPress)
    {
        _flagDrawOrSheathWeapon = true;
    }

    glm::vec2 input(0.0f);  // @COPYPASTA
    input.x += input::keyLeftPressed  ? -1.0f : 0.0f;
    input.x += input::keyRightPressed ?  1.0f : 0.0f;
    input.y += input::keyUpPressed    ?  1.0f : 0.0f;
    input.y += input::keyDownPressed  ? -1.0f : 0.0f;

    if (_camera->freeCamMode.enabled)
    {
        input = glm::vec2(0.0f);
        _flagJump = false;
        _flagDrawOrSheathWeapon = false;
    }

    glm::vec3 flatCameraFacingDirection = _camera->sceneCamera.facingDirection;
    flatCameraFacingDirection.y = 0.0f;
    flatCameraFacingDirection = glm::normalize(flatCameraFacingDirection);

    _worldSpaceInput =
        input.y * flatCameraFacingDirection +
        input.x * glm::normalize(glm::cross(flatCameraFacingDirection, glm::vec3(0, 1, 0)));*/

    // Update render transform
    if (glm::length2(_worldSpaceInput) > 0.01f)
        _facingDirection = glm::atan(_worldSpaceInput.x, _worldSpaceInput.z);

    glm::vec3 interpPos = physutil::getPosition(_physicsObj->interpolatedTransform);
    _renderObj->transformMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0)));
}

void Enemy::physicsUpdate(const float_t& physicsDeltaTime)
{
    //
    // Clear state
    //
    _onGround = false;

    //
    // Update state
    //
    _stepsSinceLastGrounded++;

    glm::vec3 velocity = physutil::toVec3(_physicsObj->body->getLinearVelocity());

    velocity -= _displacementToTarget / physicsDeltaTime;  // Undo the displacement (hopefully no movement bugs)
    _displacementToTarget = glm::vec3(0.0f);

    processGrounded(velocity, physicsDeltaTime);

    if (_flagDrawOrSheathWeapon)
    {
        //
        // Enter/exit combat mode
        //
        _isCombatMode = !_isCombatMode;

        if (_isCombatMode)
        {
            AudioEngine::getInstance().playSound("res/sfx/wip_draw_weapon.ogg");
            //_renderObj->animator->setTrigger("goto_combat_mode");
        }
        else
        {
            AudioEngine::getInstance().playSoundFromList({
                "res/sfx/wip_sheath_weapon.ogg",
                "res/sfx/wip_sheath_weapon_2.ogg"
            });
            //_renderObj->animator->setTrigger("leave_combat_mode");
        }

        // Reset everything else
        _flagDrawOrSheathWeapon = false;
        _airDashMove = false;  // @NOTE: I added this back in like 5 minutes after removing it bc the 45deg air dash is with the sword drawn, so it makes sense to nullify it if you're gonna sheath your weapon.  -Timo 2022/11/12    @NOTE: I think that removing this is important so that you can do the airborne sword drawing airdash and then put your sword away to jump easier  -Timo 2022/11/12

        // If entering combat mode and is airborne,
        // do a 45 degree downwards air dash
        if (_isCombatMode && !_onGround)
        {
            _airDashDirection = glm::vec3(0, -glm::sin(glm::radians(45.0f)), glm::cos(glm::radians(45.0f)));
            _airDashDirection = glm::quat(glm::vec3(0, _facingDirection, 0)) * _airDashDirection;

            // @COPYPASTA
            _airDashMove = true;
            _usedAirDash = true;
            _airDashPrepauseTime = 0.25f;
            _airDashPrepauseTimeElapsed = 0.0f;
            _airDashTimeElapsed = 0.0f;
            _airDashFinishSpeedFracCooked = 1.0f;
            _airDashSpeed = _airDashSpeedXZ;
        }
    }

    if (_airDashMove)
    {
        //
        // Process air dash
        //
        if (_airDashPrepauseTimeElapsed < _airDashPrepauseTime)
        {
            velocity = glm::vec3(0.0f);
            _airDashPrepauseTimeElapsed += physicsDeltaTime;
        }
        else
        {
            velocity = _airDashDirection * physutil::lerp(_airDashSpeed, _airDashSpeed * _airDashFinishSpeedFracCooked, _airDashTimeElapsed / _airDashTime);

            // First frame of actual dash, play sound
            if (_airDashTimeElapsed == 0.0f)
                AudioEngine::getInstance().playSoundFromList({
                    "res/sfx/wip_char_mad_dash_red_left.ogg",
                    "res/sfx/wip_char_mad_dash_red_right.ogg",
                    });

            _airDashTimeElapsed += physicsDeltaTime;
        }

        // Exit air dash
        if (_onGround || _airDashTimeElapsed > _airDashTime)
        {
            _airDashMove = false;
        }
    }
    else if (_isCombatMode)
    {
        //
        // Process combat mode
        //

        // @TODO

        //
        // Calculate rigidbody velocity
        // @COPYPASTA
        //
        glm::vec3 desiredVelocity = _worldSpaceInput * _maxSpeed;  // @NOTE: we just ignore the y component in this desired velocity thing

        glm::vec2 a(velocity.x, velocity.z);
        glm::vec2 b(desiredVelocity.x, desiredVelocity.z);

        /*if (_onGround)
            if (glm::length2(b) < 0.0001f)
                _renderObj->animator->setTrigger("goto_idle");
            else
                _renderObj->animator->setTrigger("goto_run");*/

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

        float_t acceleration    = _onGround ? _maxAcceleration : _maxMidairAcceleration;
        if (!useAcceleration)
            acceleration        = _onGround ? _maxDeceleration : _maxMidairDeceleration;
        float_t maxSpeedChange  = acceleration * physicsDeltaTime;

        glm::vec2 c = physutil::moveTowardsVec2(a, b, maxSpeedChange);
        velocity.x = c.x;
        velocity.z = c.y;

        // Ignore jump requests
        if (_flagJump)
        {
            _flagJump = false;
        }
    }
    else
    {
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

        /*if (_onGround)
            if (glm::length2(b) < 0.0001f)
                _renderObj->animator->setTrigger("goto_idle");
            else
                _renderObj->animator->setTrigger("goto_run");*/

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

        float_t acceleration    = _onGround ? _maxAcceleration : _maxMidairAcceleration;
        if (!useAcceleration)
            acceleration        = _onGround ? _maxDeceleration : _maxMidairDeceleration;
        float_t maxSpeedChange  = acceleration * physicsDeltaTime;

        glm::vec2 c = physutil::moveTowardsVec2(a, b, maxSpeedChange);
        velocity.x = c.x;
        velocity.z = c.y;

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

            bool jumpSuccessful = false;
            switch (jumpType)
            {
            case GROUNDED_JUMP:
                if (_onGround || (int32_t)_stepsSinceLastGrounded <= _jumpCoyoteFrames)
                {
                    // @DEBUG: if you want something to look at coyote time and jump buffering metrics, uncomment
                    //std::cout << "[JUMP INFO]" << std::endl
                    //    << "Buffer Frames left:         " << _jumpInputBufferFramesTimer << std::endl
                    //    << "Frames since last grounded: " << _stepsSinceLastGrounded << std::endl;
                    velocity.y = 
                        glm::sqrt(_jumpHeight * 2.0f * PhysicsEngine::getInstance().getGravityStrength());
                    _displacementToTarget = glm::vec3(0.0f);
                    _stepsSinceLastGrounded = _jumpCoyoteFrames;  // This is to prevent ground sticking right after a jump and multiple jumps performed right after another jump was done!

                    // @TODO: add some kind of audio event system, or even better, figure out how to use FMOD!!! Bc it's freakign integrated lol
                    AudioEngine::getInstance().playSoundFromList({
                        "res/sfx/wip_jump1.ogg",
                        "res/sfx/wip_jump2.ogg"
                        });

                    jumpSuccessful = true;                    
                }
                break;

            case AIR_DASH:
            {
                // @TODO: you're gonna have to check for jump buffer time for this bc there is a chance that the player is intending to jump on the ground despite having
                //        a jump they can do in the air. You will need to detect whether they are too close to the ground to store the jump input rather than do it as a
                //        air dash  -Timo
                _airDashDirection = glm::vec3(0, 1, 0);
                _airDashSpeed = _airDashSpeedY;
                if (glm::length2(_worldSpaceInput) > 0.0001f)
                {
                    _airDashDirection = glm::normalize(_worldSpaceInput);
                    _airDashSpeed = _airDashSpeedXZ;
                }

                _airDashMove = true;
                _usedAirDash = true;
                _airDashPrepauseTime = 0.0f;
                _airDashPrepauseTimeElapsed = 0.0f;
                _airDashTimeElapsed = 0.0f;
                _airDashFinishSpeedFracCooked = _airDashFinishSpeedFrac;

                jumpSuccessful = true;

                break;
            }

            case NONE:
                break;
            }

            // Turn off flag for sure if successfully jumped
            if (jumpSuccessful)
            {
                _jumpPreventOnGroundCheckFramesTimer = _jumpPreventOnGroundCheckFrames;
                _jumpInputBufferFramesTimer = -1;
                _flagJump = false;
            }

            // Turn off flag if jump buffer frames got exhausted
            if (_jumpInputBufferFramesTimer-- < 0)
                _flagJump = false;
        }
    }

    _physicsObj->body->setLinearVelocity(physutil::toVec3(velocity + _displacementToTarget / physicsDeltaTime));
}

void Enemy::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(physutil::getPosition(_renderObj->transformMatrix));
    ds.dumpFloat(_facingDirection);
}

void Enemy::load(DataSerialized& ds)
{
    Entity::load(ds);
    _load_position         = ds.loadVec3();
    _facingDirection       = ds.loadFloat();
}

void Enemy::renderImGui()
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

    ImGui::Separator();

    ImGui::Text(("_isCombatMode: " + std::to_string(_isCombatMode)).c_str());
    
    ImGui::Separator();

    ImGui::Text(("_airDashMove: " + std::to_string(_airDashMove)).c_str());
    ImGui::Text(("_usedAirDash: " + std::to_string(_usedAirDash)).c_str());
    ImGui::DragFloat3("_airDashDirection", &_airDashDirection[0]);
    ImGui::DragFloat("_airDashTime", &_airDashTime);
    ImGui::DragFloat("_airDashTimeElapsed", &_airDashTimeElapsed);
    ImGui::DragFloat("_airDashSpeed", &_airDashSpeed);
    ImGui::DragFloat("_airDashSpeedXZ", &_airDashSpeedXZ);
    ImGui::DragFloat("_airDashSpeedY", &_airDashSpeedY);
    ImGui::DragFloat("_airDashFinishSpeedFracCooked", &_airDashFinishSpeedFracCooked);
    ImGui::DragFloat("_airDashFinishSpeedFrac", &_airDashFinishSpeedFrac);
}

void Enemy::processGrounded(glm::vec3& velocity, const float_t& physicsDeltaTime)
{
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
        _physicsObj->body->setGravity(btVector3(0, 0, 0));
        velocity.y = 0.0f;
    }
    else
    {
        _physicsObj->body->setGravity(PhysicsEngine::getInstance().getGravity());
        /*if (_stepsSinceLastGrounded)
            _renderObj->animator->setTrigger("goto_fall");*/
    }
}

void Enemy::onCollisionStay(btPersistentManifold* manifold, bool amIB)
{
    _groundContactNormal = glm::vec3(0.0f);
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
        _groundContactNormal = glm::normalize(_groundContactNormal);
}
