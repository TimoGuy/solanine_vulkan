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


class Player : public Entity
{
public:
    static const std::string TYPE_NAME;

    Player(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds);
    ~Player();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void update(const float_t& deltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; };

    void reportMoved(void* matrixMoved);
    void renderImGui();

private:
    RenderObject*    _characterRenderObj;
    RenderObject*    _handleRenderObj;
    RenderObject*    _weaponRenderObj;
    std::string      _weaponAttachmentJointName;
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
    bool      _flagAttack             = false;
    bool      _isCombatMode           = false;
    bool      _isWeaponCollision      = false;
    glm::mat4 _weaponPrevTransform    = glm::mat4(0.0f);  // NOTE: this is the flag to show to ignore the prev transform

    struct WeaponCollision
    {
        size_t  numRays     = 7;
        float_t startOffset = 0.5f;
        float_t distance    = 5.5f;
    } _weaponCollisionProps;

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

    // Replay system
    ReplayData _replayData;
    enum class RecordingState { NONE, RECORDING, PLAYING };
    RecordingState _recordingState = RecordingState::NONE;

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
    float_t _landingApplyMassMult = 1.0f;
};
