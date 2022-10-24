#pragma once

#include <cmath>
class VulkanEngine;


class Entity
{
public:
    Entity(VulkanEngine* engine);
    virtual ~Entity();
    virtual void update(const float_t& deltaTime) { }    // Gets called once per frame
    virtual void physicsUpdate(const float_t) { }        // Gets called once per physics calculation

    // @NOTE: you need to manually enable these!
    bool _enableUpdate = false,
         _enablePhysicsUpdate = false;

protected:
    VulkanEngine* _engine;
};
