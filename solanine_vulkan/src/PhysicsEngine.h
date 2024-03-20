#pragma once

class EntityManager;
struct DeletionQueue;
namespace JPH { struct Character; }
namespace vkglTF { struct Animator; }

#ifdef _DEVELOP
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

    void requestSetRunPhysicsSimulation(bool flag);
    bool getIsRunPhysicsSimulation();

    size_t registerSimulationTransform();
    void unregisterSimulationTransform(size_t id);
    void updateSimulationTransformPosition(size_t id, vec3 pos);
    void updateSimulationTransformRotation(size_t id, versor rot);
    void getInterpSimulationTransformPosition(size_t id, vec3& outPos);
    void getInterpSimulationTransformRotation(size_t id, versor& outRot);
    void getCurrentSimulationTransformPositionAndRotation(size_t id, vec3& outPos, versor& outRot);

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
        size_t simTransformId;
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
    void moveVoxelFieldBodyKinematic(VoxelFieldPhysicsData& vfpd, vec3 newPosition, versor newRotation, float_t simDeltaTime);
    void setVoxelFieldBodyKinematic(VoxelFieldPhysicsData& vfpd, bool isKinematic);  // `false` is dynamic body.

    struct CapsulePhysicsData
    {
        std::string entityGuid;

        float_t radius;
        float_t height;
        vec3 currentCOMPosition = GLM_VEC3_ZERO_INIT;  // @DEPRECATED: doesn't work anymore!
        vec3 prevCOMPosition = GLM_VEC3_ZERO_INIT;  // @DEPRECATED: doesn't work anymore!
        vec3 interpolCOMPosition = GLM_VEC3_ZERO_INIT;  // @DEPRECATED: doesn't work anymore!
        bool COMPositionDifferent = false;  // @DEPRECATED: doesn't work anymore!
        JPH::Character* character = nullptr;
        size_t simTransformId;
    };

    CapsulePhysicsData* createCharacter(const std::string& entityGuid, vec3 position, const float_t& radius, const float_t& height, bool enableCCD);
    bool destroyCapsule(CapsulePhysicsData* cpd);
    size_t getNumCapsules();
    CapsulePhysicsData* getCapsuleByIndex(size_t index);
    float_t getLengthOffsetToBase(const CapsulePhysicsData& cpd);
    void setCharacterPosition(CapsulePhysicsData& cpd, vec3 position);
    void getCharacterPosition(const CapsulePhysicsData& cpd, vec3& outPosition);
    void moveCharacter(CapsulePhysicsData& cpd, vec3 velocity);
    void setGravityFactor(CapsulePhysicsData& cpd, float_t newGravityFactor);
    void getLinearVelocity(const CapsulePhysicsData& cpd, vec3& outVelocity);
    bool isGrounded(const CapsulePhysicsData& cpd);
    bool isSlopeTooSteepForCharacter(const CapsulePhysicsData& cpd, JPH::Vec3Arg normal);

    void recalcInterpolatedTransformsSet();

    void setWorldGravity(vec3 newGravity);
    void getWorldGravity(vec3& outGravity);
    size_t getCollisionLayer(const std::string& layerName);
    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid, float_t& outFraction, vec3& outSurfaceNormal);
    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid, float_t& outFraction);
    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid);
    bool cylinderCast(vec3 origin, float_t radius, float_t height, JPH::BodyID ignoreBodyId, vec3 directionAndMagnitude, float_t& outFraction, vec3& outSurfaceNormal);
    bool cylinderCast(vec3 origin, float_t radius, float_t height, JPH::BodyID ignoreBodyId, vec3 directionAndMagnitude, float_t& outFraction, vec3& outSurfaceNormal, vec3& outLocalContactPosition);

    // @NOTE: this is specifically used/created for overlapping hitboxes and hurtboxes.
    //        It would probably be wise to expand this function's use to be agnostic to
    //        its intended use, but for now this is to just get the functionality
    //        up and running.  -Timo 2024/03/14
    struct BodyAndSubshapeID
    {
        JPH::BodyID bodyId;
        JPH::SubShapeID subShapeId;
        vec3 hitPosition;
    };
    bool capsuleOverlap(vec3 origin, versor rotation, float_t radius, float_t height, JPH::BodyID ignoreBodyId, std::vector<BodyAndSubshapeID>& outHitIds);

    // Hitbox structure.
    struct BoundHitCapsule
    {
        std::string boneName = "";  // @NOTE: the initial setup will be expensive
                                    //        but is fine to do a string comparison.
        vec3 offset = GLM_VEC3_ZERO_INIT;
        versor rotation = GLM_QUAT_IDENTITY_INIT;
        float_t height = 1.0f;
        float_t radius = 0.5f;
    };
    typedef uint32_t sbhcs_key_t;
    const static sbhcs_key_t kInvalidSBHCSKey = (sbhcs_key_t)-1;
    sbhcs_key_t createSkeletonBoundHitCapsuleSet(std::vector<BoundHitCapsule>& hitCapsules, size_t simTransformId, vkglTF::Animator* skeleton, float_t yOffset);
    bool destroySkeletonBoundHitCapsuleSet(sbhcs_key_t key);
    void updateSkeletonBoundHitCapsuleSet(sbhcs_key_t key);
    void getAllSkeletonBoundHitCapsulesInSet(sbhcs_key_t key, std::vector<BoundHitCapsule>& outHitCapsules);
    void updateSkeletonBoundHitCapsuleInSet(sbhcs_key_t setKey, size_t index, const BoundHitCapsule& newParams);


    // Helper functions.
    std::string bodyIdToEntityGuid(JPH::BodyID bodyId);

#ifdef _DEVELOP
    enum class DebugVisLineType { PURPTEAL, AUDACITY, SUCCESS, VELOCITY, KIKKOARMY, YUUJUUFUDAN };
    void drawDebugVisCapsule(vec3 pt1, vec3 pt2, float_t radius, DebugVisLineType type = DebugVisLineType::PURPTEAL);
    void drawDebugVisLine(vec3 pt1, vec3 pt2, DebugVisLineType type = DebugVisLineType::PURPTEAL);
    void drawDebugVisPoint(vec3 pt, DebugVisLineType type = DebugVisLineType::PURPTEAL);

    void renderImguiPerformanceStats();
    void renderDebugVisualization(VkCommandBuffer cmd);
#endif
}