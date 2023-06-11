#include "PhysicsEngine.h"

#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <format>
#include "PhysUtil.h"
#include "EntityManager.h"
#include "imgui/imgui.h"
#include "imgui/implot.h"


namespace physengine
{
    //
    // Physics engine works
    //
    constexpr float_t physicsDeltaTime = 0.025f;    // 40fps. This seemed to be the sweet spot. 25/30fps would be inconsistent for getting smaller platform jumps with the dash move. 50fps felt like too many physics calculations all at once. 40fps seems right, striking a balance.  -Timo 2023/01/26
    constexpr float_t oneOverPhysicsDeltaTimeInMS = 1.0f / (physicsDeltaTime * 1000.0f);
    float_t timeScale = 1.0f;

    void runPhysicsEngineAsync();
    EntityManager* entityManager;
    bool isAsyncRunnerRunning;
    std::thread* asyncRunner = nullptr;
    uint64_t lastTick;

#ifdef _DEVELOP
    struct DebugStats
    {
        size_t simTimesUSHeadIndex = 0;
        size_t simTimesUSCount = 256;
        float_t simTimesUS[256 * 2];
        float_t highestSimTime = -1.0f;
    } perfStats;
#endif

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

    void setTimeScale(const float_t& timeScale)
    {
        physengine::timeScale = timeScale;
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
         
#ifdef _DEVELOP
            uint64_t perfTime = SDL_GetPerformanceCounter();
#endif

            // @NOTE: this is the only place where `timeScale` is used. That's
            //        because this system is designed to be running at 40fps constantly
            //        in real time, so it doesn't slow down or speed up with time scale.
            // @REPLY: I thought that the system should just run in a constant 40fps. As in,
            //         if the timescale slows down, then the tick rate should also slow down
            //         proportionate to the timescale.  -Timo 2023/06/10
            tick();
            entityManager->INTERNALphysicsUpdate(physicsDeltaTime * timeScale);

#ifdef _DEVELOP
            {
                //
                // Update performance metrics
                // @COPYPASTA
                //
                perfTime = SDL_GetPerformanceCounter() - perfTime;
                perfStats.simTimesUSHeadIndex = (size_t)std::fmodf((float_t)perfStats.simTimesUSHeadIndex + 1, (float_t)perfStats.simTimesUSCount);

                // Find what the highest simulation time is
                if (perfTime > perfStats.highestSimTime)
                    perfStats.highestSimTime = perfTime;
                else if (perfStats.simTimesUS[perfStats.simTimesUSHeadIndex] == perfStats.highestSimTime)
                {
                    // Former highest sim time is getting overwritten; recalculate the 2nd highest sim time.
                    float_t nextHighestsimTime = perfTime;
                    for (size_t i = perfStats.simTimesUSHeadIndex + 1; i < perfStats.simTimesUSHeadIndex + perfStats.simTimesUSCount; i++)
                        nextHighestsimTime = std::max(nextHighestsimTime, perfStats.simTimesUS[i]);
                    perfStats.highestSimTime = nextHighestsimTime;
                }

                // Apply simulation time to buffer
                perfStats.simTimesUS[perfStats.simTimesUSHeadIndex] =
                    perfStats.simTimesUS[perfStats.simTimesUSHeadIndex + perfStats.simTimesUSCount] =
                    perfTime;
            }
#endif

            // Wait for remaining time
            uint64_t endingTime = SDL_GetTicks64();
            uint64_t timeDiff = endingTime - lastTick;
            if (timeDiff > physicsDeltaTimeInMS)
            {
                std::cerr << "ERROR: physics engine is running too slowly. (" << (timeDiff - physicsDeltaTimeInMS) << "ns behind)" << std::endl;
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
                        float_t dpSqrDist = glm_vec3_norm2(deltaPoint);
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

    void moveCapsuleAccountingForCollision(CapsulePhysicsData& cpd, vec3 deltaPosition, bool stickToGround, vec3& outNormal, float_t ccdDistance)
    {
        glm_vec3_zero(outNormal);  // In case if no collision happens, normal is zero'd!

        do
        {
            // // @NOTE: keep this code here. It works sometimes (if the edge capsule walked up to is flat) and is useful for reference.
            // vec3 originalPosition;
            // glm_vec3_copy(cpd.basePosition, originalPosition);

            vec3 deltaPositionCCD;
            glm_vec3_copy(deltaPosition, deltaPositionCCD);
            if (glm_vec3_norm2(deltaPosition) > ccdDistance * ccdDistance) // Move at a max of the ccdDistance
                glm_vec3_scale_as(deltaPosition, ccdDistance, deltaPositionCCD);
            glm_vec3_sub(deltaPosition, deltaPositionCCD, deltaPosition);

            // Move and check for collision
            glm_vec3_zero(outNormal);
            float_t numNormals = 0.0f;

            glm_vec3_add(cpd.basePosition, deltaPositionCCD, cpd.basePosition);

            for (size_t iterations = 0; iterations < 6; iterations++)
            {
                vec3 normal;
                float_t penetrationDepth;
                bool collided = physengine::debugCheckCapsuleColliding(cpd, normal, penetrationDepth);

                // Subsequent iterations of collision are just to resolve until sitting in empty space,
                // so only double check 1st iteration if expecting to stick to the ground.
                if (iterations == 0 && !collided && stickToGround)
                {
                    vec3 oldPosition;
                    glm_vec3_copy(cpd.basePosition, oldPosition);

                    cpd.basePosition[1] += -ccdDistance;  // Just push straight down maximum amount to see if collides
                    collided = physengine::debugCheckCapsuleColliding(cpd, normal, penetrationDepth);
                    if (!collided)
                        glm_vec3_copy(oldPosition, cpd.basePosition);  // I guess this empty space was where the capsule was supposed to go to after all!
                }
                
                // Resolved into empty space.
                // Do not proceed to do collision resolution.
                if (!collided)
                    break;

                // Collided!
                glm_vec3_add(outNormal, normal, outNormal);
                penetrationDepth += 0.0001f;
                if (normal[1] >= 0.707106781187)  // >=45 degrees
                {
                    // Don't slide on "level-enough" ground
                    cpd.basePosition[1] += penetrationDepth / normal[1];
                }
                else
                {
                    vec3 penetrationDepthV3 = { penetrationDepth, penetrationDepth, penetrationDepth };
                    glm_vec3_muladd(normal, penetrationDepthV3, cpd.basePosition);
                }
            }

            if (numNormals != 0.0f)
                glm_vec3_scale(outNormal, 1.0f / numNormals, outNormal);

            // // Keep capsule from falling off the edge!
            // // @NOTE: keep this code here. It works sometimes (if the edge capsule walked up to is flat) and is useful for reference.
            // if (stickToGround && (glm_vec3_norm2(outNormal) < 0.000001f || outNormal[1] < 0.707106781187))
            // {
            //     if (glm_vec3_norm2(outNormal) > 0.000001f && glm_vec3_norm2(deltaPosition) > 0.000001f)
            //     {
            //         // Redirect rest of ccd movement along the cross of up and the bad normal
            //         vec3 upXBadNormal;
            //         glm_cross(vec3{ 0.0f, 1.0f, 0.0f }, outNormal, upXBadNormal);
            //         glm_normalize(upXBadNormal);

            //         vec3 deltaPositionFlat = {
            //             deltaPosition[0] + deltaPositionCCD[0],
            //             0.0f,
            //             deltaPosition[2] + deltaPositionCCD[2],
            //         };
            //         float_t deltaPositionY = deltaPosition[1] + deltaPositionCCD[1];
            //         float_t deltaPositionFlatLength = glm_vec3_norm(deltaPositionFlat);
            //         glm_normalize(deltaPositionFlat);

            //         float_t slideSca = glm_dot(upXBadNormal, deltaPositionFlat);
            //         glm_vec3_scale(upXBadNormal, slideSca * deltaPositionFlatLength, deltaPosition);
            //         deltaPosition[1] = deltaPositionY;
            //     }


            //     std::cout << "DONT JUMP!\tX: " << outNormal[0] << "\tY: " << outNormal[1] << "\tZ: " << outNormal[2] << std::endl;
            //     outNormal[0] = outNormal[2] = 0.0f;
            //     outNormal[1] = 1.0f;

            //     glm_vec3_copy(originalPosition, cpd.basePosition);
            // }
        } while (glm_vec3_norm2(deltaPosition) > 0.000001f);
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
                glm_translate(transform, interpolPos);
                glm_quat_rotate(transform, interpolRot, transform);
                glm_scale(transform, interpolSca);
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

#ifdef _DEVELOP
    void renderImguiPerformanceStats()
    {
        static const float_t perfTimeToMS = 1000.0f / (float_t)SDL_GetPerformanceFrequency();
        ImGui::Text("Physics Times");
        ImGui::Text((std::format("{:.2f}", perfStats.simTimesUS[perfStats.simTimesUSHeadIndex] * perfTimeToMS) + "ms").c_str());
        ImGui::PlotHistogram("##Physics Times Histogram", perfStats.simTimesUS, (int32_t)perfStats.simTimesUSCount, (int32_t)perfStats.simTimesUSHeadIndex, "", 0.0f, perfStats.highestSimTime, ImVec2(256, 24.0f));
        ImGui::SameLine();
        ImGui::Text(("[0, " + std::format("{:.2f}", perfStats.highestSimTime * perfTimeToMS) + "]").c_str());
    }
#endif
}