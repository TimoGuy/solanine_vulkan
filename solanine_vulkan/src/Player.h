#pragma once

#include "Entity.h"
#include "Imports.h"
#include "VkglTFModel.h"
struct RenderObject;
struct RegisteredPhysicsObject;


class Player : public Entity
{
public:
    Player(VulkanEngine* engine);
    ~Player();

    void update(const float_t& deltaTime);
    void physicsUpdate(const float_t& physicsDeltaTime);

private:
    glm::vec3 _position;
    float_t _facingDirection;

    vkglTF::Model* _characterModel;
    RenderObject* _renderObj;
    RegisteredPhysicsObject* _physicsObj;
    RegisteredPhysicsObject* _physicsObj2;

    bool _flagJump = false;
    float_t _maxSpeed = 10.0f;
    float_t _maxAcceleration = 50.0f;
    float_t _jumpHeight = 5.0f;
};