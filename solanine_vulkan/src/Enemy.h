#pragma once

#include "Entity.h"
#include "ReplaySystem.h"
#include "Imports.h"
namespace vkglTF { struct Model; }
struct RenderObject;
class RenderObjectManager;
class EntityManager;
struct RegisteredPhysicsObject;
struct Camera;
class btPersistentManifold;
class btCapsuleShape;


class Enemy : public Entity
{
public:
    static const std::string TYPE_NAME;

    Enemy(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds);
    ~Enemy();

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

    void processGrounded(glm::vec3& velocity, const float_t& physicsDeltaTime);

    glm::vec3 _worldSpaceInput = glm::vec3(0.0f);
    bool      _flagJump        = false;

    bool      _onGround = false;
    glm::vec3 _groundContactNormal;
    uint32_t  _stepsSinceLastGrounded = 0;
    glm::vec3 _displacementToTarget = glm::vec3(0.0f);

    int32_t   _jumpPreventOnGroundCheckFramesTimer = -1;
    int32_t   _jumpInputBufferFramesTimer          = -1;

    // Combat mode
    bool      _flagDrawOrSheathWeapon = false;
    bool      _isCombatMode           = false;

    // Air dash move
    bool      _airDashMove                = false;
    bool      _usedAirDash                = false;
    glm::vec3 _airDashDirection;
    float_t   _airDashPrepauseTime        = 0.0f;
    float_t   _airDashPrepauseTimeElapsed;
    float_t   _airDashTime                = 0.25f;
    float_t   _airDashTimeElapsed;
    float_t   _airDashSpeed;  // Cooked value
    float_t   _airDashSpeedXZ             = 100.0f;
    float_t   _airDashSpeedY              = 50.0f;
    float_t   _airDashFinishSpeedFracCooked;
    float_t   _airDashFinishSpeedFrac     = 0.25f;

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
    int32_t _jumpPreventOnGroundCheckFrames = 4;
    int32_t _jumpCoyoteFrames = 6;       // @NOTE: frames are measured with the constant 0.02f seconds per frame in the physics delta time
    int32_t _jumpInputBufferFrames = 4;  //        Thus, 4 frames in that measurement is 4.8 frames in 60 fps
};
