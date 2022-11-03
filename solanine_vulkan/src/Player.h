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

    vkglTF::Model* _characterModel;
    RenderObject* _renderObj;
    btCapsuleShape* _collisionShape;
    RegisteredPhysicsObject* _physicsObj;
    float_t _totalHeight;
    float_t _maxClimbAngle;
    float_t _capsuleRadius;
    float_t _bottomRaycastFeetDist;
    float_t _bottomRaycastExtraDist;
    float_t _adjustedHalfHeight;

    bool _onGround = false;
    glm::vec3 _groundContactNormal;
    uint32_t _stepsSinceLastGrounded = 0;
    bool _flagJump = false;
    glm::vec3 _prevPosition;
    glm::vec3 _displacementToTarget = glm::vec3(0.0f);

    // Callbacks
    std::function<void(btPersistentManifold*, bool amIB)> _onCollisionStayFunc;
    void onCollisionStay(btPersistentManifold* manifold, bool amIB);

    // Load Props
    glm::vec3 _load_position = glm::vec3(0.0f);
    glm::vec3 _load_transformOffset = glm::vec3(0, -2.5f, 0);

    // Tweak Props
    float_t _facingDirection = 0.0f;
    float_t _maxSpeed = 20.0f;
    float_t _maxAcceleration = 50.0f;
    float_t _maxMidairAcceleration = 20.0f;
    float_t _jumpHeight = 5.0f;
};
