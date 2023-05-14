#pragma once

#include "ImportGLM.h"


namespace physengine
{
    constexpr size_t PHYSICS_OBJECTS_MAX_CAPACITY = 10000;

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
        glm::mat4 transform = glm::mat4(1.0f);
    };

    VoxelFieldPhysicsData* createVoxelField(const size_t& sizeX, const size_t& sizeY, const size_t& sizeZ, uint8_t* voxelData);
    bool destroyVoxelField(VoxelFieldPhysicsData* vfpd);

    struct CapsulePhysicsData
    {
        float_t radius;
        float_t height;
        glm::vec3 basePosition = glm::vec3(0.0f);  // It takes `radius + 0.5 * height` to get to the midpoint
    };

    CapsulePhysicsData* createCapsule(const float_t& radius, const float_t& height);
    bool destroyCapsule(CapsulePhysicsData* cpd);

    bool debugCheckPointColliding(const glm::vec3& point);
    bool debugCheckCapsuleColliding(const CapsulePhysicsData& cpd);
}