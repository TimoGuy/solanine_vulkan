#pragma once

#include "ImportGLM.h"
class EntityManager;


namespace physengine
{
    void initialize(EntityManager* em);
    void cleanup();

    float_t getPhysicsAlpha();

    struct VoxelFieldPhysicsData
    {
        //
        // @NOTE: VOXEL DATA GUIDE
        //
        // Will implement:
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
    };

    VoxelFieldPhysicsData* createVoxelField(const size_t& sizeX, const size_t& sizeY, const size_t& sizeZ, uint8_t* voxelData);
    bool destroyVoxelField(VoxelFieldPhysicsData* vfpd);
    uint8_t getVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z);

    struct CapsulePhysicsData
    {
        float_t radius;
        float_t height;
        vec3 basePosition = GLM_VEC3_ZERO_INIT;  // It takes `radius + 0.5 * height` to get to the midpoint
        vec3 prevBasePosition = GLM_VEC3_ZERO_INIT;
        vec3 interpolBasePosition = GLM_VEC3_ZERO_INIT;
    };

    CapsulePhysicsData* createCapsule(const float_t& radius, const float_t& height);
    bool destroyCapsule(CapsulePhysicsData* cpd);

    bool debugCheckPointColliding(const vec3& point);
    bool debugCheckCapsuleColliding(const CapsulePhysicsData& cpd, vec3& collisionNormal, float_t& penetrationDepth);
    void moveCapsuleAccountingForCollision(CapsulePhysicsData& cpd, vec3 deltaPosition, float_t ccdDistance = 0.25f);  // @NOTE: `ccdDistance` is fine as long as it's below the capsule radius (or the radius of the voxels, whichever is smaller)

    void setPhysicsObjectInterpolation(const float_t& physicsAlpha);
}