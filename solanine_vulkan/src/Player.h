#pragma once

#include "Entity.h"
#include "Imports.h"
namespace vkglTF { class Model; }
struct RenderObject;
struct RenderObjectManager;
class EntityManager;
struct RegisteredPhysicsObject;
struct Camera;
class btPersistentManifold;
class btCapsuleShape;


class Player : public Entity
{
public:
    static const std::string TYPE_NAME;

    Player(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds);
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
    RenderObjectManager* _rom;
    btCapsuleShape* _collisionShape;
    RegisteredPhysicsObject* _physicsObj;
    Camera* _camera;
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
    glm::vec3 _displacementToTarget = glm::vec3(0.0f);

    int32_t _jumpInputBufferFramesTimer = -1;

    // Callbacks
    std::function<void(btPersistentManifold*, bool amIB)> _onCollisionStayFunc;
    void onCollisionStay(btPersistentManifold* manifold, bool amIB);

    // Load Props
    glm::vec3 _load_position = glm::vec3(0.0f);

    // Tweak Props
    float_t _facingDirection = 0.0f;
    float_t _maxSpeed = 20.0f;
    float_t _maxAcceleration = 150.0f;
    float_t _maxDeceleration = 150.0f;
    float_t _maxMidairAcceleration = 80.0f;
    float_t _maxMidairDeceleration = 20.0f;
    float_t _jumpHeight = 5.0f;
    int32_t _jumpCoyoteFrames = 4;       // @NOTE: frames are measured with the constant 0.02f seconds per frame in the physics delta time
    int32_t _jumpInputBufferFrames = 4;  //        Thus, 4 frames in that measurement is 4.8 frames in 60 fps
};
