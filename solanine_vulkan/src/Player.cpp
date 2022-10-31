#include "Player.h"

#include "VulkanEngine.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "InputManager.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"


Player::Player(VulkanEngine* engine, DataSerialized* ds) : Entity(engine, ds)
{
    if (ds == nullptr)
    {
        _position = glm::vec3(0.0f);
        _facingDirection = 0.0f;
    }
    else
        load(*ds);

    _prevPosition = _position;

    _characterModel = _engine->getModel("slimeGirl");

    _renderObj =
        _engine->registerRenderObject({
            .model = _characterModel,
            .transformMatrix = glm::translate(glm::mat4(1.0f), _position) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0))),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });
    _engine->setMainCamTargetObject(_renderObj);  // @NOTE: I believe that there should be some kind of main camera system that targets the player by default but when entering different volumes etc. the target changes depending.... essentially the system needs to be more built out imo

    _totalHeight = 5.0f;
    _maxClimbAngle= glm::radians(47.0f);
    float_t r = 0.5f;
    float_t d = (r - r * glm::sin(_maxClimbAngle)) / glm::sin(_maxClimbAngle);
    _collisionShape = new btCapsuleShape(r, _totalHeight - d);  // @NOTE: it appears that this shape has a margin in the direction of the sausage (i.e. Y in this case) and then the radius is the actual radius
    _adjustedHalfHeight = (_totalHeight - d) * 0.5 + _collisionShape->getMargin();
    _bottomRaycastFeetDist = d;
    _bottomRaycastExtraDist = 0.5f;

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            1.0f,
            _position,
            glm::quat(glm::vec3(0.0f)),
            _collisionShape
        );
    _physicsObj->transformOffset = glm::vec3(0, -2.5f, 0);
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

Player::~Player()
{
    _engine->unregisterRenderObject(_renderObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    // @TODO: figure out if I need to call `delete _collisionShape;` or not
}

void Player::update(const float_t& deltaTime)
{
    _flagJump |= input::onKeyJumpPress;

    //
    // Calculate render object transform
    //
    glm::vec3 interpPos = physutil::getPosition(_physicsObj->interpolatedTransform);
    glm::vec3 deltaPos = interpPos - _prevPosition;
    _prevPosition = interpPos;

    if (glm::length2(deltaPos) > 0.0001f)
        _facingDirection = glm::atan(deltaPos.x, deltaPos.z);

    _renderObj->transformMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0)));
}

void Player::physicsUpdate(const float_t& physicsDeltaTime)
{
    //
    // Calculate input
    //
    glm::vec2 input(0.0f);
	input.x += input::keyLeftPressed ? -1.0f : 0.0f;
	input.x += input::keyRightPressed ? 1.0f : 0.0f;
	input.y += input::keyUpPressed ? 1.0f : 0.0f;
	input.y += input::keyDownPressed ? -1.0f : 0.0f;

    if (_engine->_freeCamMode.enabled)
    {
        input = glm::vec2(0.0f);
        _flagJump = false;
    }

    glm::vec3 flatCameraFacingDirection = _engine->_sceneCamera.facingDirection;
    flatCameraFacingDirection.y = 0.0f;
    flatCameraFacingDirection = glm::normalize(flatCameraFacingDirection);

    glm::vec3 cameraViewInput =
        input.y * flatCameraFacingDirection +
		input.x * glm::normalize(glm::cross(flatCameraFacingDirection, glm::vec3(0, 1, 0)));

    if (glm::length2(cameraViewInput) < 0.01f)
        cameraViewInput = glm::vec3(0.0f);
    else
        cameraViewInput = physutil::clampVector(cameraViewInput, 0.0f, 1.0f);

    //
    // Update state
    //
    glm::vec3 velocity = physutil::toVec3(_physicsObj->body->getLinearVelocity());
    velocity -= _displacementToTarget;  // Undo the displacement (hopefully no movement bugs)
    _displacementToTarget = glm::vec3(0.0f);

    const float_t targetLength = _adjustedHalfHeight + _bottomRaycastFeetDist;
    const float_t rayLength = targetLength + _bottomRaycastExtraDist;
    auto bodyPos = _physicsObj->body->getWorldTransform().getOrigin();
    auto hitInfo = PhysicsEngine::getInstance().raycast(bodyPos, bodyPos + btVector3(0, -rayLength, 0));
    if (hitInfo.hasHit())
    {
        // @NOTE: _bottomRaycastExtraDist is meant for if the body is moving upwards like on a slope,
        //        and then needs to get snapped down, this keeps the body pulled to the ground.
        //        If the body is level or moving downwards, then we only want to include the actual
        //        regular _bottomRaycastFeetDist length. Thus we do an extra check that the hit length
        //        was smaller or equal to that length.
        if (velocity.y > 0.0f)
            _onGround = true;
        else if (hitInfo.m_closestHitFraction * rayLength <= targetLength)
            _onGround = true;

        if (_onGround)  // Only attempt to correct the distance from ground and this floating body if "_onGround"
        {
            float_t targetLengthDifference = targetLength - hitInfo.m_closestHitFraction * rayLength;
            _displacementToTarget = glm::vec3(0, targetLengthDifference, 0) / physicsDeltaTime;  // Move up even though raycast was down bc we want to go the opposite direction the raycast went.
        }
    }







    _stepsSinceLastGrounded++;
    if (_onGround)// || snapToGround(physicsDeltaTime, velocity))
    {
        _stepsSinceLastGrounded = 0;
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
    glm::vec3 desiredVelocity = cameraViewInput * _maxSpeed;  // @NOTE: we just ignore the y component in this desired velocity thing
    float_t acceleration = _onGround ? _maxAcceleration : _maxMidairAcceleration;
    float_t maxSpeedChange = acceleration * physicsDeltaTime;

    glm::vec2 a(velocity.x, velocity.z);
    glm::vec2 b(desiredVelocity.x, desiredVelocity.z);
    glm::vec2 c = physutil::moveTowardsVec2(a, b, maxSpeedChange);
    velocity.x = c.x;
    velocity.z = c.y;

    if (_flagJump)
    {
        _flagJump = false;
        if (_onGround)
        {
            velocity.y = 
                glm::sqrt(_jumpHeight * 2.0f * PhysicsEngine::getInstance().getGravityStrength());
            _displacementToTarget = glm::vec3(0.0f);
        }
    }

    _physicsObj->body->setLinearVelocity(physutil::toVec3(velocity + _displacementToTarget));

    //
    // Clear state
    //
    _onGround = false;
}

void Player::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(_position);
    ds.dumpFloat(_facingDirection);
    ds.dumpFloat(_maxSpeed);
    ds.dumpFloat(_maxAcceleration);
    ds.dumpFloat(_maxMidairAcceleration);
    ds.dumpFloat(_jumpHeight);
    ds.dumpFloat(_enableSnapping);
    ds.dumpFloat(_maxSnapSpeed);
}

void Player::load(DataSerialized& ds)
{
    Entity::load(ds);
    _position              = ds.loadVec3();
    _facingDirection       = ds.loadFloat();
    _maxSpeed              = ds.loadFloat();
    _maxAcceleration       = ds.loadFloat();
    _maxMidairAcceleration = ds.loadFloat();
    _jumpHeight            = ds.loadFloat();
    _enableSnapping        = (bool)ds.loadFloat();
    _maxSnapSpeed          = ds.loadFloat();
}

void Player::renderImGui()
{
    ImGui::Text(("_onGround: " + std::to_string(_onGround)).c_str());
    ImGui::DragFloat("_maxSpeed", &_maxSpeed);
    ImGui::DragFloat("_maxAcceleration", &_maxAcceleration);
    ImGui::DragFloat("_maxMidairAcceleration", &_maxMidairAcceleration);
    ImGui::DragFloat("_jumpHeight", &_jumpHeight);
    ImGui::Checkbox("_enableSnapping", &_enableSnapping);
    ImGui::DragFloat("_maxSnapSpeed", &_maxSnapSpeed);
}

bool Player::snapToGround(const float_t& physicsDeltaTime, glm::vec3& currentVelocity)
{
    if (!_enableSnapping)
        return false;

    if (_stepsSinceLastGrounded > 1)
        return false;

    float_t veloMagnitude2 = glm::length2(currentVelocity);
    if (veloMagnitude2 < 0.0001f || veloMagnitude2 > _maxSnapSpeed * _maxSnapSpeed)
        return false;

    float_t rayLength = _collisionShape->getRadius() * 2.0f;
    rayLength += _collisionShape->getHalfHeight() + _collisionShape->getRadius() + _collisionShape->getMargin();  // Padding to push raylength out of collision shape shell downwards
    auto bodyPos = _physicsObj->body->getWorldTransform().getOrigin();
    auto hitInfo = PhysicsEngine::getInstance().raycast(bodyPos, bodyPos + btVector3(0, -rayLength, 0));
    if (!hitInfo.hasHit())
        return false;

    if (hitInfo.m_hitNormalWorld.y() <= glm::cos(glm::radians(47.0f)))
        return false;

    _groundContactNormal = physutil::toVec3(hitInfo.m_hitNormalWorld);
    float_t veloDNormal = glm::dot(glm::normalize(currentVelocity), _groundContactNormal);
    if (veloDNormal > 0.0f)
        currentVelocity = glm::normalize(currentVelocity - _groundContactNormal * veloDNormal) * glm::sqrt(veloMagnitude2);  // Correct the velocity

    return true;
}

void Player::onCollisionStay(btPersistentManifold* manifold, bool amIB)
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
