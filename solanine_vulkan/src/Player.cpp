#include "Player.h"

#include "VulkanEngine.h"
#include "PhysicsEngine.h"
#include "InputManager.h"


Player::Player(VulkanEngine* engine) : Entity(engine)
{
    _position = glm::vec3(0.0f);
    _facingDirection = 0.0f;

    _characterModel = &_engine->_renderObjectModels.slimeGirl;

    _renderObj =
        _engine->registerRenderObject({
            .model = _characterModel,
            .transformMatrix = glm::translate(glm::mat4(1.0f), _position) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0))),
            .renderLayer = RenderLayer::VISIBLE,
            });

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            1.0f,
            _position,
            glm::quat(glm::vec3(0.0f)),
            new btCapsuleShape(0.5f, 5.0f)
        );
    _physicsObj->transformOffset = glm::vec3(0, -2.5f, 0);
    _physicsObj->body->setAngularFactor(0.0f);
    _physicsObj->body->setDamping(0.0f, 0.0f);
    _physicsObj->body->setFriction(0.0f);
    _physicsObj->body->setActivationState(DISABLE_DEACTIVATION);

    _physicsObj2 =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            glm::vec3(0, -50, 0),
            glm::quat(glm::vec3(0.0f)),
            new btBoxShape({100, 1, 100})
        );

    _enableUpdate = true;
    _enablePhysicsUpdate = true;
}

Player::~Player()
{
    _engine->unregisterRenderObject(_renderObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj2);
}

void Player::update(const float_t& deltaTime)
{
    _flagJump |= input::onKeyJumpPress;

    _renderObj->transformMatrix = _physicsObj->interpolatedTransform;
}

void Player::physicsUpdate(const float_t& physicsDeltaTime)
{
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
    // Calculate rigidbody velocity
    //
    glm::vec3 desiredVelocity = cameraViewInput * _maxSpeed;  // @NOTE: we just ignore the y component in this desired velocity thing
    float_t maxSpeedChange = _maxAcceleration * physicsDeltaTime;

    btVector3 velocity = _physicsObj->body->getLinearVelocity();
    velocity.setX(physutil::moveTowards(velocity.x(), desiredVelocity.x, maxSpeedChange));
    velocity.setZ(physutil::moveTowards(velocity.z(), desiredVelocity.z, maxSpeedChange));
    if (_flagJump)
    {
        _flagJump = false;
        velocity.setY(
            glm::sqrt(_jumpHeight * 2.0f * PhysicsEngine::getInstance().getGravityStrength())
        );
    }
    _physicsObj->body->setLinearVelocity(velocity);
}
