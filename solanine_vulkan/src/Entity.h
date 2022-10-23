#pragma once

#include <math.h>
class VulkanEngine;


class Entity
{
public:
    Entity(VulkanEngine* engine);
    virtual ~Entity();
    virtual void update(const float_t& deltaTime) { }    // Gets called once per frame
    virtual void physicsUpdate(const float_t) { }        // Gets called once per physics calculation

    bool enableUpdate = false,
        enablePhysicsUpdate = false;

protected:
    VulkanEngine* engine;
};
