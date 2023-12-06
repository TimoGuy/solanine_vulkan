#pragma once

#include <string>
#include <vector>
#include "ImportGLM.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/MathTypes.h>
class EntityManager;
struct DeletionQueue;
namespace JPH { struct Character; }

#ifdef _DEVELOP
#include <vulkan/vulkan.h>
class VulkanEngine;
#endif

namespace physengine
{
    extern bool isInitialized;

#ifdef _DEVELOP
    void initDebugVisDescriptors(VulkanEngine* engine);
    void initDebugVisPipelines(VkRenderPass mainRenderPass, VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor, DeletionQueue& deletionQueue);
    void savePhysicsWorldSnapshot();
#endif

    void start(EntityManager* em);
    void haltAsyncRunner();
    void cleanup();

    float_t getPhysicsAlpha();

    struct VoxelFieldPhysicsData
    {
        std::string entityGuid;

        //
        // @NOTE: VOXEL DATA GUIDE
        //
        // Currently implemented:
        //        0: empty space
        //        1: filled space
        //        2: slope space (uphill North)
        //        3: slope space (uphill East)
        //        4: slope space (uphill South)
        //        5: slope space (uphill West)
        //
        // For the future:
        //        half-height space (bottom)
        //        half-height space (top)
        //        4-step stair space (South 0.0 height, North 0.5 height)
        //        4-step stair space (South 0.5 height, North 1.0 height)
        //        4-step stair space (North 0.0 height, South 0.5 height)
        //        4-step stair space (North 0.5 height, South 1.0 height)
        //        4-step stair space (East  0.0 height, West  0.5 height)
        //        4-step stair space (East  0.5 height, West  1.0 height)
        //        4-step stair space (West  0.0 height, East  0.5 height)
        //        4-step stair space (West  0.5 height, East  1.0 height)
        //
        size_t sizeX, sizeY, sizeZ;
        uint8_t* voxelData;
        mat4 transform = GLM_MAT4_IDENTITY_INIT;
        mat4 prevTransform = GLM_MAT4_IDENTITY_INIT;
        mat4 interpolTransform = GLM_MAT4_IDENTITY_INIT;
        JPH::BodyID bodyId;
    };

    struct VoxelFieldCollisionShape
    {
        vec3   origin;
        versor rotation;
        vec3   extent;
    };

    VoxelFieldPhysicsData* createVoxelField(const std::string& entityGuid, mat4 transform, const size_t& sizeX, const size_t& sizeY, const size_t& sizeZ, uint8_t* voxelData);
    bool destroyVoxelField(VoxelFieldPhysicsData* vfpd);
    uint8_t getVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z);
    bool setVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z, uint8_t data);
    void expandVoxelFieldBounds(VoxelFieldPhysicsData& vfpd, ivec3 boundsMin, ivec3 boundsMax, ivec3& outOffset);
    void shrinkVoxelFieldBoundsAuto(VoxelFieldPhysicsData& vfpd, ivec3& outOffset);
    void cookVoxelDataIntoShape(VoxelFieldPhysicsData& vfpd, const std::string& entityGuid, std::vector<VoxelFieldCollisionShape>& outShapes);
    void setVoxelFieldBodyTransform(VoxelFieldPhysicsData& vfpd, vec3 newPosition, versor newRotation);
    void moveVoxelFieldBodyKinematic(VoxelFieldPhysicsData& vfpd, vec3 newPosition, versor newRotation, const float_t& physicsDeltaTime);
    void setVoxelFieldBodyKinematic(VoxelFieldPhysicsData& vfpd, bool isKinematic);  // `false` is dynamic body.

    struct CapsulePhysicsData
    {
        std::string entityGuid;

        float_t radius;
        float_t height;
        JPH::Character* character = nullptr;
        vec3 currentCOMPosition = GLM_VEC3_ZERO_INIT;
        vec3 prevCOMPosition = GLM_VEC3_ZERO_INIT;
        vec3 interpolCOMPosition = GLM_VEC3_ZERO_INIT;
        bool COMPositionDifferent = false;
    };

    CapsulePhysicsData* createCharacter(const std::string& entityGuid, vec3 position, const float_t& radius, const float_t& height, bool enableCCD);
    bool destroyCapsule(CapsulePhysicsData* cpd);
    size_t getNumCapsules();
    CapsulePhysicsData* getCapsuleByIndex(size_t index);
    float_t getLengthOffsetToBase(const CapsulePhysicsData& cpd);
    void setCharacterPosition(CapsulePhysicsData& cpd, vec3 position);
    void moveCharacter(CapsulePhysicsData& cpd, vec3 velocity);
    void setGravityFactor(CapsulePhysicsData& cpd, float_t newGravityFactor);
    void getLinearVelocity(const CapsulePhysicsData& cpd, vec3& outVelocity);
    bool isGrounded(const CapsulePhysicsData& cpd);
    bool isSlopeTooSteepForCharacter(const CapsulePhysicsData& cpd, JPH::Vec3Arg normal);

#if 0
    void moveCapsuleAccountingForCollision(CapsulePhysicsData& cpd, vec3 deltaPosition, bool stickToGround, vec3& outNormal, float_t ccdDistance = 0.25f);  // @NOTE: `ccdDistance` is fine as long as it's below the capsule radius (or the radius of the voxels, whichever is smaller)
#endif

    void setPhysicsObjectInterpolation(const float_t& physicsAlpha);

    void setWorldGravity(vec3 newGravity);
    size_t getCollisionLayer(const std::string& layerName);
    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid);
#if 0
    bool lineSegmentCast(vec3& pt1, vec3& pt2, size_t collisionLayer, bool getAllGuids, std::vector<std::string>& outHitGuid);
#endif

#ifdef _DEVELOP
    enum DebugVisLineType { PURPTEAL, AUDACITY, SUCCESS, VELOCITY, KIKKOARMY, YUUJUUFUDAN };
    void drawDebugVisLine(vec3 pt1, vec3 pt2, DebugVisLineType type = PURPTEAL);

    void renderImguiPerformanceStats();
    void renderDebugVisualization(VkCommandBuffer cmd);
#endif
}