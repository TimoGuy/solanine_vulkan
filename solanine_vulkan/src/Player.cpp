#include "Player.h"

#include "VulkanEngine.h"
#include "PhysicsEngine.h"
#include "InputManager.h"
#include "imgui/imgui.h"


Player::Player(VulkanEngine* engine) : Entity(engine)
{
    _position = glm::vec3(0.0f);
    _prevPosition = _position;
    _facingDirection = 0.0f;

    _characterModel = &_engine->_renderObjectModels.slimeGirl;

    _renderObj =
        _engine->registerRenderObject({
            .model = _characterModel,
            .transformMatrix = glm::translate(glm::mat4(1.0f), _position) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0))),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = _guid,
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

    _physicsObj2 =  // @TEMP (0deg)
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            glm::vec3(0, -10, 0),
            glm::quat(glm::vec3(0.0f)),
            new btBoxShape({100, 1, 100})
        );
    _physicsObj3 =  // @TEMP (45deg)
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            glm::vec3(90, 20, -50),
            glm::quat(glm::vec3(0.785398, 0.0f, 0.0f)),
            new btBoxShape({20, 1, 100})
        );
    _physicsObj3 =  // @TEMP (30deg)
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            glm::vec3(50, 10, -50),
            glm::quat(glm::vec3(0.523599, 0.0f, 0.0f)),
            new btBoxShape({20, 1, 100})
        );
    _physicsObj3 =  // @TEMP (22.5deg)
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            glm::vec3(10, 5, -50),
            glm::quat(glm::vec3(0.3926991, 0.0f, 0.0f)),
            new btBoxShape({20, 1, 100})
        );
    _physicsObj3 =  // @TEMP (10deg)
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            glm::vec3(-30, 2, -50),
            glm::quat(glm::vec3(0.174533, 0.0f, 0.0f)),
            new btBoxShape({20, 1, 100})
        );
    _physicsObj3 =  // @TEMP (5deg)
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            glm::vec3(-70, -5, -50),
            glm::quat(glm::vec3(0.0872665, 0.0f, 0.0f)),
            new btBoxShape({20, 1, 100})
        );

    _enableUpdate = true;
    _enablePhysicsUpdate = true;
}

Player::~Player()
{
    _engine->unregisterRenderObject(_renderObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj2);  // @TEMP
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj3);  // @TEMP
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

void Player::renderImGui()
{
    ImGui::DragFloat("_maxSpeed", &_maxSpeed);
    ImGui::DragFloat("_maxAcceleration", &_maxAcceleration);
    ImGui::DragFloat("_jumpHeight", &_jumpHeight);
}
