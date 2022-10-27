#pragma once

#include "Entity.h"
#include "Imports.h"
#include "VkglTFModel.h"
struct RenderObject;
struct RegisteredPhysicsObject;
class btPersistentManifold;


class Player : public Entity
{
public:
    Player(VulkanEngine* engine);
    ~Player();

    void update(const float_t& deltaTime);
    void physicsUpdate(const float_t& physicsDeltaTime);

    void renderImGui();

private:
    glm::vec3 _position;
    float_t _facingDirection;

    vkglTF::Model* _characterModel;
    RenderObject* _renderObj;
    RegisteredPhysicsObject* _physicsObj;
    RegisteredPhysicsObject* _physicsObj2;
    RegisteredPhysicsObject* _physicsObj3;

    bool _onGround = false;
    bool _flagJump = false;
    glm::vec3 _prevPosition;

    std::function<void(btPersistentManifold*)> _onCollisionStayFunc;
    void onCollisionStay(btPersistentManifold* manifold);

    // Tweak Props
    float_t _maxSpeed = 20.0f;
    float_t _maxAcceleration = 50.0f;
    float_t _maxMidairAcceleration = 20.0f;
    float_t _jumpHeight = 5.0f;
};