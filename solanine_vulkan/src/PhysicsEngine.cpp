#include "PhysicsEngine.h"

#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include "PhysUtil.h"
#include "EntityManager.h"


namespace physengine
{
    //
    // Physics engine works
    //
    constexpr float_t physicsDeltaTime = 0.025f;    // 40fps. This seemed to be the sweet spot. 25/30fps would be inconsistent for getting smaller platform jumps with the dash move. 50fps felt like too many physics calculations all at once. 40fps seems right, striking a balance.  -Timo 2023/01/26
    constexpr float_t oneOverPhysicsDeltaTimeInMS = 1.0f / (physicsDeltaTime * 1000.0f);

    void runPhysicsEngineAsync();
    EntityManager* entityManager;
    bool isAsyncRunnerRunning;
    std::thread* asyncRunner = nullptr;
    uint64_t lastTick;

    void initialize(EntityManager* em)
    {
        entityManager = em;
        isAsyncRunnerRunning = true;
        asyncRunner = new std::thread(runPhysicsEngineAsync);
    }

    void cleanup()
    {
        isAsyncRunnerRunning = false;
        asyncRunner->join();
        delete asyncRunner;
    }

    float_t getPhysicsAlpha()
    {
        return (SDL_GetTicks64() - lastTick) * oneOverPhysicsDeltaTimeInMS;
    }

    void tick();

    void runPhysicsEngineAsync()
    {
        constexpr uint64_t physicsDeltaTimeInMS = physicsDeltaTime * 1000.0f;

        while (isAsyncRunnerRunning)
        {
            lastTick = SDL_GetTicks64();
            
            tick();
            entityManager->INTERNALphysicsUpdate(physicsDeltaTime);

            // Wait for remaining time
            uint64_t endingTime = SDL_GetTicks64();
            uint64_t timeDiff = endingTime - lastTick;
            if (timeDiff > physicsDeltaTimeInMS)
            {
                std::cerr << "ERROR: physics engine is running too slow. (" << (timeDiff - physicsDeltaTimeInMS) << "ns behind)" << std::endl;
            }
            else
            {
                SDL_Delay((uint32_t)(physicsDeltaTimeInMS - timeDiff));
            }
        }
    }

    //
    // Voxel field pool
    //
    constexpr size_t PHYSICS_OBJECTS_MAX_CAPACITY = 10000;

    VoxelFieldPhysicsData voxelFieldPool[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t voxelFieldIndices[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t numVFsCreated = 0;

    VoxelFieldPhysicsData* createVoxelField(const size_t& sizeX, const size_t& sizeY, const size_t& sizeZ, uint8_t* voxelData)
    {
        if (numVFsCreated < PHYSICS_OBJECTS_MAX_CAPACITY)
        {
            // Pull a voxel field from the pool
            size_t index = 0;
            if (numVFsCreated > 0)
            {
                index = (voxelFieldIndices[numVFsCreated - 1] + 1) % PHYSICS_OBJECTS_MAX_CAPACITY;
                voxelFieldIndices[numVFsCreated] = index;
            }
            VoxelFieldPhysicsData& vfpd = voxelFieldPool[index];
            numVFsCreated++;

            // Insert in the data
            vfpd.sizeX = sizeX;
            vfpd.sizeY = sizeY;
            vfpd.sizeZ = sizeZ;
            vfpd.voxelData = voxelData;
            
            return &vfpd;
        }
        else
        {
            std::cerr << "ERROR: voxel field creation has reached its limit" << std::endl;
            return nullptr;
        }
    }

    bool destroyVoxelField(VoxelFieldPhysicsData* vfpd)
    {
        for (size_t i = 0; i < numVFsCreated; i++)
        {
            size_t& index = voxelFieldIndices[i];
            if (&voxelFieldPool[index] == vfpd)
            {
                if (numVFsCreated > 1)
                {
                    // Overwrite the index with the back index,
                    // effectively deleting the index
                    index = voxelFieldIndices[numVFsCreated - 1];
                }
                numVFsCreated--;
                return true;
            }
        }
        return false;
    }

    uint8_t getVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z)
    {
        if (x < 0           || y < 0           || z < 0          ||
            x >= vfpd.sizeX || y >= vfpd.sizeY || z >= vfpd.sizeZ)
            return 0;
        return vfpd.voxelData[(size_t)x * vfpd.sizeY * vfpd.sizeZ + (size_t)y * vfpd.sizeZ + (size_t)z];
    }

    //
    // Capsule pool
    //
    CapsulePhysicsData capsulePool[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t capsuleIndices[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t numCapsCreated = 0;

    CapsulePhysicsData* createCapsule(const float_t& radius, const float_t& height)
    {
        if (numCapsCreated < PHYSICS_OBJECTS_MAX_CAPACITY)
        {
            // Pull a capsule from the pool
            size_t index = 0;
            if (numCapsCreated > 0)
            {
                index = (capsuleIndices[numCapsCreated - 1] + 1) % PHYSICS_OBJECTS_MAX_CAPACITY;
                capsuleIndices[numCapsCreated] = index;
            }
            CapsulePhysicsData& cpd = capsulePool[index];
            numCapsCreated++;

            // Insert in the data
            cpd.radius = radius;
            cpd.height = height;
            
            return &cpd;
        }
        else
        {
            std::cerr << "ERROR: capsule creation has reached its limit" << std::endl;
            return nullptr;
        }
    }

    bool destroyCapsule(CapsulePhysicsData* cpd)
    {
        for (size_t i = 0; i < numCapsCreated; i++)
        {
            size_t& index = capsuleIndices[i];
            if (&capsulePool[index] == cpd)
            {
                if (numCapsCreated > 1)
                {
                    // Overwrite the index with the back index,
                    // effectively deleting the index
                    index = capsuleIndices[numCapsCreated - 1];
                }
                numCapsCreated--;
                return true;
            }
        }
        return false;
    }

    //
    // Tick
    //
    void tick()
    {
        // Set previous transform
        for (size_t i = 0; i < numVFsCreated; i++)
        {
            VoxelFieldPhysicsData& vfpd = voxelFieldPool[voxelFieldIndices[i]];
            glm_mat4_copy(vfpd.transform, vfpd.prevTransform);
        }
        for (size_t i = 0; i < numCapsCreated; i++)
        {
            CapsulePhysicsData& cpd = capsulePool[capsuleIndices[i]];
            glm_vec3_copy(cpd.basePosition, cpd.prevBasePosition);
        }
    }

    //
    // Collision algorithms
    //
    void closestPointToLineSegment(vec3& pt, vec3& a, vec3& b, vec3 &outPt)
    {
        // https://arrowinmyknee.com/2021/03/15/some-math-about-capsule-collision/
        vec3 ab;
        glm_vec3_sub(b, a, ab);

        // Project pt onto ab, but deferring divide by Dot(ab, ab)
        vec3 pt_a;
        glm_vec3_sub(pt, a, pt_a);
        float_t t = glm_vec3_dot(pt_a, ab);
        if (t <= 0.0f)
        {
            // pt projects outside the [a,b] interval, on the a side; clamp to a
            t = 0.0f;
            glm_vec3_copy(a, outPt);
        }
        else
        {
            float_t denom = glm_vec3_dot(ab, ab); // Always nonnegative since denom = ||ab||∧2
            if (t >= denom)
            {
                // pt projects outside the [a,b] interval, on the b side; clamp to b
                t = 1.0f;
                glm_vec3_copy(b, outPt);
            }
            else
            {
                // pt projects inside the [a,b] interval; must do deferred divide now
                t = t / denom;
                glm_vec3_scale(ab, t, ab);
                glm_vec3_add(a, ab, outPt);
            }
        }
    }

    bool checkCapsuleCollidingWithVoxelField(VoxelFieldPhysicsData& vfpd, CapsulePhysicsData& cpd, vec3& collisionNormal, float_t& penetrationDepth)
    {
        //
        // Broad phase: turn both objects into AABB and do collision
        //
        auto broadPhaseTimingStart = std::chrono::high_resolution_clock::now();

        vec3 capsulePtATransformed;
        vec3 capsulePtBTransformed;
        mat4 vfpdTransInv;
        glm_mat4_inv(vfpd.transform, vfpdTransInv);
        glm_vec3_copy(cpd.basePosition, capsulePtATransformed);
        glm_vec3_copy(cpd.basePosition, capsulePtBTransformed);
        capsulePtATransformed[1] += cpd.radius + cpd.height;
        capsulePtBTransformed[1] += cpd.radius;
        glm_mat4_mulv3(vfpdTransInv, capsulePtATransformed, 1.0f, capsulePtATransformed);
        glm_mat4_mulv3(vfpdTransInv, capsulePtBTransformed, 1.0f, capsulePtBTransformed);
        vec3 capsuleAABBMinMax[2] = {
            {
                std::min(capsulePtATransformed[0], capsulePtBTransformed[0]) - cpd.radius,  // @NOTE: add/subtract the radius while in voxel field transform space.
                std::min(capsulePtATransformed[1], capsulePtBTransformed[1]) - cpd.radius,
                std::min(capsulePtATransformed[2], capsulePtBTransformed[2]) - cpd.radius
            },
            {
                std::max(capsulePtATransformed[0], capsulePtBTransformed[0]) + cpd.radius,
                std::max(capsulePtATransformed[1], capsulePtBTransformed[1]) + cpd.radius,
                std::max(capsulePtATransformed[2], capsulePtBTransformed[2]) + cpd.radius
            },
        };
        vec3 voxelFieldAABBMinMax[2] = {
            { 0.0f, 0.0f, 0.0f },
            { vfpd.sizeX, vfpd.sizeY, vfpd.sizeZ },
        };
        if (capsuleAABBMinMax[0][0] > voxelFieldAABBMinMax[1][0] ||
            capsuleAABBMinMax[1][0] < voxelFieldAABBMinMax[0][0] ||
            capsuleAABBMinMax[0][1] > voxelFieldAABBMinMax[1][1] ||
            capsuleAABBMinMax[1][1] < voxelFieldAABBMinMax[0][1] ||
            capsuleAABBMinMax[0][2] > voxelFieldAABBMinMax[1][2] ||
            capsuleAABBMinMax[1][2] < voxelFieldAABBMinMax[0][2])
            return false;

        auto broadPhaseTimingDiff = std::chrono::high_resolution_clock::now() - broadPhaseTimingStart;

        //
        // Narrow phase: check all filled voxels within the capsule AABB
        //
        auto narrowPhaseTimingStart = std::chrono::high_resolution_clock::now();
        ivec3 searchMin = {
            std::max(floor(capsuleAABBMinMax[0][0]), voxelFieldAABBMinMax[0][0]),
            std::max(floor(capsuleAABBMinMax[0][1]), voxelFieldAABBMinMax[0][1]),
            std::max(floor(capsuleAABBMinMax[0][2]), voxelFieldAABBMinMax[0][2])
        };
        ivec3 searchMax = {
            std::min(floor(capsuleAABBMinMax[1][0]), voxelFieldAABBMinMax[1][0] - 1),
            std::min(floor(capsuleAABBMinMax[1][1]), voxelFieldAABBMinMax[1][1] - 1),
            std::min(floor(capsuleAABBMinMax[1][2]), voxelFieldAABBMinMax[1][2] - 1)
        };

        bool collisionSuccessful = false;
        float_t lowestDpSqrDist = std::numeric_limits<float_t>::max();
        size_t lkjlkj = 0;
        size_t succs = 0;
        for (size_t i = searchMin[0]; i <= searchMax[0]; i++)
        for (size_t j = searchMin[1]; j <= searchMax[1]; j++)
        for (size_t k = searchMin[2]; k <= searchMax[2]; k++)
        {
            lkjlkj++;
            uint8_t vd = vfpd.voxelData[i * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + k];

            switch (vd)
            {
                // Empty space
                case 0:
                    continue;

                // Filled space
                case 1:
                {
                    //
                    // Test collision with this voxel
                    //
                    vec3 voxelCenterPt = { i + 0.5f, j + 0.5f, k + 0.5f };
                    vec3 point;
                    closestPointToLineSegment(voxelCenterPt, capsulePtATransformed, capsulePtBTransformed, point);

                    vec3 boundedPoint;
                    glm_vec3_copy(point, boundedPoint);
                    boundedPoint[0] = glm_clamp(boundedPoint[0], i, i + 1.0f);
                    boundedPoint[1] = glm_clamp(boundedPoint[1], j, j + 1.0f);
                    boundedPoint[2] = glm_clamp(boundedPoint[2], k, k + 1.0f);
                    if (point == boundedPoint)
                    {
                        // Collider is stuck inside
                        collisionNormal[0] = 0.0f;
                        collisionNormal[1] = 1.0f;
                        collisionNormal[2] = 0.0f;
                        penetrationDepth = 1.0f;
                        return true;
                    }
                    else
                    {
                        // Get more accurate point with the bounded point
                        vec3 betterPoint;
                        closestPointToLineSegment(boundedPoint, capsulePtATransformed, capsulePtBTransformed, betterPoint);

                        vec3 deltaPoint;
                        glm_vec3_sub(betterPoint, boundedPoint, deltaPoint);
                        float_t dpSqrDist = glm_vec3_dot(deltaPoint, deltaPoint);
                        if (dpSqrDist < cpd.radius * cpd.radius && dpSqrDist < lowestDpSqrDist)
                        {
                            // Collision successful
                            succs++;
                            collisionSuccessful = true;
                            lowestDpSqrDist = dpSqrDist;
                            glm_normalize(deltaPoint);
                            mat4 transformCopy;
                            glm_mat4_copy(vfpd.transform, transformCopy);
                            glm_mat4_mulv3(transformCopy, deltaPoint, 0.0f, collisionNormal);
                            penetrationDepth = cpd.radius - std::sqrt(dpSqrDist);
                        }
                    }
                } break;
            }
        }

        auto narrowPhaseTimingDiff = std::chrono::high_resolution_clock::now() - narrowPhaseTimingStart;
        // std::cout << "collided: checks: " << lkjlkj << "\tsuccs: " << succs << "\ttime (broad): " << broadPhaseTimingDiff  << "\ttime (narrow): " << narrowPhaseTimingDiff << "\tisGround: " << (collisionNormal[1] >= 0.707106665647) << "\tnormal: " << collisionNormal[0] << ", " << collisionNormal[1] << ", " << collisionNormal[2] << "\tdepth: " << penetrationDepth << std::endl;

        return collisionSuccessful;
    }

    bool debugCheckCapsuleColliding(CapsulePhysicsData& cpd, vec3& collisionNormal, float_t& penetrationDepth)
    {
        vec3 normal;
        float_t penDepth;

        for (size_t i = 0; i < numVFsCreated; i++)
        {
            size_t& index = voxelFieldIndices[i];
            if (checkCapsuleCollidingWithVoxelField(voxelFieldPool[index], cpd, normal, penDepth))
            {
                glm_vec3_copy(normal, collisionNormal);
                penetrationDepth = penDepth;
                return true;
            }
        }
        return false;
    }

    void moveCapsuleAccountingForCollision(CapsulePhysicsData& cpd, vec3 deltaPosition, float_t ccdDistance)
    {
        do
        {
            if (glm_vec3_dot(deltaPosition, deltaPosition) > ccdDistance * ccdDistance)
            {
                // Move at a max of the ccdDistance
                vec3 m;
                glm_vec3_normalize_to(deltaPosition, m);
                glm_vec3_scale(m, ccdDistance, m);
                glm_vec3_add(cpd.basePosition, m, cpd.basePosition);
                glm_vec3_sub(deltaPosition, m, deltaPosition);
            }
            else
            {
                // Move the rest of the way
                glm_vec3_add(cpd.basePosition, deltaPosition, cpd.basePosition);
                deltaPosition[0] = deltaPosition[1] = deltaPosition[2] = 0.0f;
            }

            // Check for collision
            for (size_t iterations = 0; iterations < 6; iterations++)
            {
                vec3 normal;
                float_t penetrationDepth;
                if (physengine::debugCheckCapsuleColliding(cpd, normal, penetrationDepth))
                {
                    penetrationDepth += 0.0001f;
                    if (normal[1] >= 0.707106781187)  // >=45 degrees
                    {
                        // Stick to the ground
                        cpd.basePosition[1] += penetrationDepth / normal[1];
                    }
                    else
                    {
                        vec3 penetrationDepthV3 = { penetrationDepth, penetrationDepth, penetrationDepth };
                        glm_vec3_muladd(normal, penetrationDepthV3, cpd.basePosition);
                    }
                }
                else
                    break;
            }
        } while (glm_vec3_dot(deltaPosition, deltaPosition) > 0.000001f);
    }

    void setPhysicsObjectInterpolation(const float_t& physicsAlpha)
    {
        //
        // Set interpolated transform
        //
        for (size_t i = 0; i < numVFsCreated; i++)
        {
            VoxelFieldPhysicsData& vfpd = voxelFieldPool[voxelFieldIndices[i]];
            if (vfpd.prevTransform != vfpd.transform)
            {
                vec4   prevPositionV4, positionV4;
                vec3   prevPosition,   position;
                mat4   prevRotationM4, rotationM4;
                versor prevRotation,   rotation;
                vec3   prevScale,      scale;
                glm_decompose(vfpd.prevTransform, prevPositionV4, prevRotationM4, prevScale);
                glm_decompose(vfpd.transform, positionV4, rotationM4, scale);
                glm_vec4_copy3(prevPositionV4, prevPosition);
                glm_vec4_copy3(positionV4, position);
                glm_mat4_quat(prevRotationM4, prevRotation);
                glm_mat4_quat(rotationM4, rotation);

                vec3 interpolPos;
                glm_vec3_lerp(prevPosition, position, physicsAlpha, interpolPos);
                versor interpolRot;
                glm_quat_nlerp(prevRotation, rotation, physicsAlpha, interpolRot);
                vec3 interpolSca;
                glm_vec3_lerp(prevScale, scale, physicsAlpha, interpolSca);

                mat4 transform = GLM_MAT4_IDENTITY_INIT;
                glm_scale(transform, interpolSca);
                glm_quat_rotate(transform, interpolRot, transform);
                glm_translate(transform, interpolPos);
                glm_mat4_copy(transform, vfpd.interpolTransform);
            }
        }
        for (size_t i = 0; i < numCapsCreated; i++)
        {
            CapsulePhysicsData& cpd = capsulePool[capsuleIndices[i]];
            if (cpd.prevBasePosition != cpd.basePosition)
            {
                glm_vec3_lerp(cpd.prevBasePosition, cpd.basePosition, physicsAlpha, cpd.interpolBasePosition);
            }
        }
    }
}