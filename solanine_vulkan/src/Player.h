#pragma once

#include "Entity.h"
#include "ReplaySystem.h"
#include "Imports.h"
namespace vkglTF { struct Model; }
struct RenderObject;
class RenderObjectManager;
class EntityManager;
struct Camera;


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
    Camera* _camera;

    glm::vec3 _worldSpaceInput = glm::vec3(0.0f);

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
