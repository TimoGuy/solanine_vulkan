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
class btRigidBody;


class Player : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; };

    Player(EntityManager* em, RenderObjectManager* rom, Camera* camera, DataSerialized* ds);
    ~Player();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void update(const float_t& deltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    bool processMessage(DataSerialized& message);

    void reportMoved(void* matrixMoved);
    void renderImGui();

private:
    RenderObject*            _characterRenderObj;
    RenderObject*            _handleRenderObj;
    RenderObject*            _weaponRenderObj;
    std::string              _weaponAttachmentJointName;
    RenderObjectManager*     _rom;
    btCapsuleShape*          _collisionShape;
    RegisteredPhysicsObject* _physicsObj;
    Camera* _camera;
    float_t _totalHeight;
    float_t _maxClimbAngle;
    float_t _capsuleRadius;
    float_t _bottomRaycastFeetDist;
    float_t _bottomRaycastExtraDist;
    float_t _adjustedHalfHeight;
    
    float_t _attackedDebounce = 0.25f;
    float_t _attackedDebounceTimer = 0.0f;

    void processGrounded(glm::vec3& velocity, float_t& groundAccelMult, const float_t& physicsDeltaTime);

    glm::vec3 _worldSpaceInput = glm::vec3(0.0f);
    bool      _flagJump        = false;

    bool      _onGround = false;
    glm::vec3 _groundContactNormal;
    uint32_t  _stepsSinceLastGrounded = 0;
    glm::vec3 _displacementToTarget = glm::vec3(0.0f);
    glm::vec3 _windZoneVelocity = glm::vec3(0.0f);
    int32_t   _windZoneSFXChannelId = -1;
    int32_t   _windZoneOccupancyPrevEnum = 0;

    int32_t   _jumpPreventOnGroundCheckFramesTimer = -1;
    int32_t   _jumpInputBufferFramesTimer          = -1;

    // Moving platforms
    btRigidBody* _attachedBody            = nullptr;
    bool         _isAttachedBodyStale     = true;
    int32_t      _framesSinceAttachedBody = 0;
    glm::vec3    _attachmentWorldPosition;
    glm::vec3    _attachmentLocalPosition;
    glm::vec3    _attachmentVelocity      = { 0, 0, 0 };
    glm::vec3    _prevAttachmentVelocity  = { 0, 0, 0 };

    // Combat mode
    bool      _isWeaponDrawn             = false;
    glm::mat4 _weaponPrevTransform       = glm::mat4(0.0f);  // NOTE: this is the flag to show to ignore the prev transform

    enum class AttackStage { NONE, PREPAUSE, SWING, CHAIN_COMBO, END };
    enum class AttackType { HORIZONTAL, DIVE_ATTACK, SPIN_ATTACK };
    bool        _flagAttack                = false;
    bool        _allowComboInput           = false;
    bool        _allowComboTransition      = false;
    bool        _usedSpinAttack            = false;  // You shan't use this multiple times in the air!
    AttackStage _attackStage               = AttackStage::NONE;
    AttackType  _attackType;
    bool        _attackPrepauseReady       = false;
    float_t     _attackPrepauseTime        = 0.291667f;  // Equivalent of 7 frames of animation @24 fps
    float_t     _attackPrepauseTimeElapsed = 0.0f;
    float_t     _attackSwingTimeElapsed    = 0.0f;
    float_t     _spinAttackUpwardsSpeed    = 30.0f;
    void startAttack(AttackType type);
    void processAttackStageSwing(glm::vec3& velocity, const float_t& physicsDeltaTime);

    struct WeaponCollision
    {
        size_t  numRays     = 12;
        float_t startOffset = -1.0f;
        float_t distance    = 8.0f;
    } _weaponCollisionProps;
    void processWeaponCollision();

    // Air dash move
    bool      _airDashMove                = false;
    bool      _usedAirDash                = false;
    glm::vec3 _airDashDirection;
    float_t   _airDashTime                = 0.25f;
    float_t   _airDashTimeElapsed;
    float_t   _airDashSpeed               = 100.0f;
    float_t   _airDashFinishSpeedFracCooked;
    float_t   _airDashFinishSpeedFrac     = 0.25f;
    
    // Being grabbed data
    struct BeingGrabbedData
    {
        uint32_t stage = 0;  // 0: none; 1: is being grabbed; 2: is being kicked out
        glm::vec3 gotoPosition;
        float_t gotoFacingDirection;
        glm::vec3 kickoutVelocity;
    } _beingGrabbedData;

    // Replay system
    ReplayData _replayData;
    enum class RecordingState { NONE, RECORDING, PLAYING };
    RecordingState _recordingState = RecordingState::NONE;

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
    float_t _landingApplyMassMult = 50.0f;
};
