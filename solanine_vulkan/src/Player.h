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
    void physicsUpdate(const float_t);

private:
    glm::vec3 _position;
    float_t _facingDirection;

    vkglTF::Model* _characterModel;
    RenderObject* _renderObj;
    RegisteredPhysicsObject* _physicsObj;
    RegisteredPhysicsObject* _physicsObj2;
};