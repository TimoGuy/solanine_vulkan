#pragma once

#include <cmath>
#include <string>
class VulkanEngine;


class Entity
{
public:
    Entity(VulkanEngine* engine);
    virtual ~Entity();
    virtual void update(const float_t& deltaTime) { }    // Gets called once per frame
    virtual void physicsUpdate(const float_t& physicsDeltaTime) { }        // Gets called once per physics calculation
    std::string getGUID() { return _guid; }

    // @NOTE: you need to manually enable these!
    bool _enableUpdate = false,
         _enablePhysicsUpdate = false;

protected:
    VulkanEngine* _engine;
    std::string _guid;
};
