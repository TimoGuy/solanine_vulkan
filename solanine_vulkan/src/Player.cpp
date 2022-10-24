#include "Player.h"

#include "VulkanEngine.h"
#include "PhysicsEngine.h"


Player::Player(VulkanEngine* engine) : Entity(engine)
{
    _position = glm::vec3(0.0f);
    _facingDirection = 0.0f;

    _characterModel.loadFromFile(_engine, "res/models/SlimeGirl.glb");

    _renderObj =
        _engine->registerRenderObject({
            .model = &_characterModel,
            .transformMatrix = glm::translate(glm::mat4(1.0f), _position) * glm::toMat4(glm::quat(glm::vec3(0, _facingDirection, 0))),
            .renderLayer = RenderLayer::VISIBLE,
            });

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            1.0f,
            _position,
            glm::quat(glm::vec3(0.0f)),
            new btCapsuleShape(0.5f, 2.0f)
        );

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
    _characterModel.destroy(_engine->_allocator);
    _engine->unregisterRenderObject(_renderObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj2);
}

void Player::update(const float_t& deltaTime)
{
    _renderObj->transformMatrix = _physicsObj->interpolatedTransform;
}

void Player::physicsUpdate(const float_t)
{
    
}
