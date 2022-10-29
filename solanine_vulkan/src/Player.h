#pragma once

#include "Entity.h"
#include "Imports.h"
namespace vkglTF { class Model; }
struct RenderObject;
struct RegisteredPhysicsObject;
class btPersistentManifold;
class btCapsuleShape;


class Player : public Entity
{
public:
    static const std::string TYPE_NAME;

    Player(VulkanEngine* engine, DataSerialized* ds);
    ~Player();

    void update(const float_t& deltaTime);
    void physicsUpdate(const float_t& physicsDeltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; };

    void renderImGui();

private:
    glm::vec3 _position;
    float_t _facingDirection;

    vkglTF::Model* _characterModel;
    RenderObject* _renderObj;
    btCapsuleShape* _collisionShape;
    RegisteredPhysicsObject* _physicsObj;
    RegisteredPhysicsObject* _physicsObj2;
    RegisteredPhysicsObject* _physicsObj3;

    bool _onGround = false;
    glm::vec3 _groundContactNormal;
    uint32_t _stepsSinceLastGrounded = 0;
    bool _flagJump = false;
    glm::vec3 _prevPosition;

    bool snapToGround(const float_t& physicsDeltaTime, glm::vec3& currentVelocity);

    // Callbacks
    std::function<void(btPersistentManifold*)> _onCollisionStayFunc;
    void onCollisionStay(btPersistentManifold* manifold);

    // Tweak Props
    float_t _maxSpeed = 20.0f;
    float_t _maxAcceleration = 50.0f;
    float_t _maxMidairAcceleration = 20.0f;
    float_t _jumpHeight = 5.0f;
    bool    _enableSnapping = true;
    float_t _maxSnapSpeed = 100.0f;
};
