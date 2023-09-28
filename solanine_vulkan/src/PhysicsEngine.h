#pragma once

#include <string>
#include <vector>
#include "ImportGLM.h"
class EntityManager;
struct DeletionQueue;
namespace JPH { struct Body; }

#ifdef _DEVELOP
#include <vulkan/vulkan.h>
class VulkanEngine;
#endif

namespace physengine
{
#ifdef _DEVELOP
    void initDebugVisDescriptors(VulkanEngine* engine);
    void initDebugVisPipelines(VkRenderPass mainRenderPass, VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor, DeletionQueue& deletionQueue);
    void savePhysicsWorldSnapshot();
#endif

    void start(EntityManager* em);
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
        //
        // For the future:
        //        2: half-height space (bottom)
        //        3: half-height space (top)
        //        4: 4-step stair space (South 0.0 height, North 0.5 height)
        //        5: 4-step stair space (South 0.5 height, North 1.0 height)
        //        6: 4-step stair space (North 0.0 height, South 0.5 height)
        //        7: 4-step stair space (North 0.5 height, South 1.0 height)
        //        8: 4-step stair space (East  0.0 height, West  0.5 height)
        //        9: 4-step stair space (East  0.5 height, West  1.0 height)
        //       10: 4-step stair space (West  0.0 height, East  0.5 height)
        //       11: 4-step stair space (West  0.5 height, East  1.0 height)
        //
        size_t sizeX, sizeY, sizeZ;
        uint8_t* voxelData;
        mat4 transform = GLM_MAT4_IDENTITY_INIT;
        mat4 prevTransform = GLM_MAT4_IDENTITY_INIT;
        mat4 interpolTransform = GLM_MAT4_IDENTITY_INIT;
        JPH::Body* body = nullptr;
    };

    VoxelFieldPhysicsData* createVoxelField(const std::string& entityGuid, const size_t& sizeX, const size_t& sizeY, const size_t& sizeZ, uint8_t* voxelData);
    bool destroyVoxelField(VoxelFieldPhysicsData* vfpd);
    uint8_t getVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z);
    bool setVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z, uint8_t data);
    void expandVoxelFieldBounds(VoxelFieldPhysicsData& vfpd, ivec3 boundsMin, ivec3 boundsMax, ivec3& outOffset);
    void shrinkVoxelFieldBoundsAuto(VoxelFieldPhysicsData& vfpd, ivec3& outOffset);
    void asdfalskdjflkasdhflkahsdlgkh(VoxelFieldPhysicsData& vfpd); // @NOCHECKIN
    void cookVoxelDataIntoShape(VoxelFieldPhysicsData& vfpd);

    struct CapsulePhysicsData
    {
        std::string entityGuid;

        float_t radius;
        float_t height;
        vec3 basePosition = GLM_VEC3_ZERO_INIT;  // It takes `radius + 0.5 * height` to get to the midpoint
        vec3 prevBasePosition = GLM_VEC3_ZERO_INIT;
        vec3 interpolBasePosition = GLM_VEC3_ZERO_INIT;
    };

    CapsulePhysicsData* createCapsule(const std::string& entityGuid, const float_t& radius, const float_t& height);
    bool destroyCapsule(CapsulePhysicsData* cpd);
    size_t getNumCapsules();
    CapsulePhysicsData* getCapsuleByIndex(size_t index);

    void moveCapsuleAccountingForCollision(CapsulePhysicsData& cpd, vec3 deltaPosition, bool stickToGround, vec3& outNormal, float_t ccdDistance = 0.25f);  // @NOTE: `ccdDistance` is fine as long as it's below the capsule radius (or the radius of the voxels, whichever is smaller)

    void setPhysicsObjectInterpolation(const float_t& physicsAlpha);

    size_t getCollisionLayer(const std::string& layerName);
    bool lineSegmentCast(vec3& pt1, vec3& pt2, size_t collisionLayer, bool getAllGuids, std::vector<std::string>& outHitGuid);

#ifdef _DEVELOP
    enum DebugVisLineType { PURPTEAL, AUDACITY, SUCCESS, VELOCITY, KIKKOARMY, YUUJUUFUDAN };
    void drawDebugVisLine(vec3 pt1, vec3 pt2, DebugVisLineType type = PURPTEAL);

    void renderImguiPerformanceStats();
    void renderDebugVisualization(VkCommandBuffer cmd);
#endif
}