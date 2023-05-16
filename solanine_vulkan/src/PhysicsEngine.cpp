#include "PhysicsEngine.h"

#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
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
            vfpd.prevTransform = vfpd.transform;
        }
        for (size_t i = 0; i < numCapsCreated; i++)
        {
            CapsulePhysicsData& cpd = capsulePool[capsuleIndices[i]];
            cpd.prevBasePosition = cpd.basePosition;
        }
    }

    //
    // Collision algorithms
    //
    bool checkPointCollidingWithVoxelField(const VoxelFieldPhysicsData& vfpd, const glm::vec3& point)
    {
        glm::vec3 transformedPoint = glm::inverse(vfpd.transform) * glm::vec4(point, 1.0f);

        // Check bounding box
        if (transformedPoint.x < 0           || transformedPoint.y < 0           || transformedPoint.z < 0          ||
            transformedPoint.x >= vfpd.sizeX || transformedPoint.y >= vfpd.sizeY || transformedPoint.z >= vfpd.sizeZ)
            return false;

        // Check if point is filled
        uint8_t vd = vfpd.voxelData[(size_t)glm::floor(transformedPoint.x) * vfpd.sizeY * vfpd.sizeZ + (size_t)glm::floor(transformedPoint.y) * vfpd.sizeZ + (size_t)glm::floor(transformedPoint.z)];
        return (vd == 1);
    }

    bool debugCheckPointColliding(const glm::vec3& point)
    {
        for (size_t i = 0; i < numVFsCreated; i++)
        {
            size_t& index = voxelFieldIndices[i];
            if (checkPointCollidingWithVoxelField(voxelFieldPool[index], point))
                return true;
        }
        return false;
    }

    void closestPointToLineSegment(const glm::vec3& pt, const glm::vec3& a, const glm::vec3& b, glm::vec3 &outPt)
    {
        // https://arrowinmyknee.com/2021/03/15/some-math-about-capsule-collision/
        glm::vec3 ab = b - a;

        // Project pt onto ab, but deferring divide by Dot(ab, ab)
        float_t t = glm::dot(pt - a, ab);
        if (t <= 0.0f)
        {
            // pt projects outside the [a,b] interval, on the a side; clamp to a
            t = 0.0f;
            outPt = a;
        }
        else
        {
            float_t denom = glm::dot(ab, ab); // Always nonnegative since denom = ||ab||∧2
            if (t >= denom)
            {
                // pt projects outside the [a,b] interval, on the b side; clamp to b
                t = 1.0f;
                outPt = b;
            }
            else
            {
                // pt projects inside the [a,b] interval; must do deferred divide now
                t = t / denom;
                outPt = a + t * ab;
            }
        }
    }

    bool checkCapsuleCollidingWithVoxelField(const VoxelFieldPhysicsData& vfpd, const CapsulePhysicsData& cpd, glm::vec3& collisionNormal, float_t& penetrationDepth)
    {
        //
        // Broad phase: turn both objects into AABB and do collision
        //
        auto broadPhaseTimingStart = std::chrono::high_resolution_clock::now();

        glm::vec3 capsulePtATransformed = glm::inverse(vfpd.transform) * glm::vec4(cpd.basePosition + glm::vec3(0, cpd.radius + cpd.height, 0), 1.0f);
        glm::vec3 capsulePtBTransformed = glm::inverse(vfpd.transform) * glm::vec4(cpd.basePosition + glm::vec3(0, cpd.radius, 0), 1.0f);
        glm::vec3 capsuleAABBMinMax[2] = {
            glm::vec3(
                glm::min(capsulePtATransformed.x, capsulePtBTransformed.x) - cpd.radius,  // @NOTE: add/subtract the radius while in voxel field transform space.
                glm::min(capsulePtATransformed.y, capsulePtBTransformed.y) - cpd.radius,
                glm::min(capsulePtATransformed.z, capsulePtBTransformed.z) - cpd.radius
            ),
            glm::vec3(
                glm::max(capsulePtATransformed.x, capsulePtBTransformed.x) + cpd.radius,
                glm::max(capsulePtATransformed.y, capsulePtBTransformed.y) + cpd.radius,
                glm::max(capsulePtATransformed.z, capsulePtBTransformed.z) + cpd.radius
            ),
        };
        glm::vec3 voxelFieldAABBMinMax[2] = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(vfpd.sizeX, vfpd.sizeY, vfpd.sizeZ),
        };
        if (capsuleAABBMinMax[0].x > voxelFieldAABBMinMax[1].x ||
            capsuleAABBMinMax[1].x < voxelFieldAABBMinMax[0].x ||
            capsuleAABBMinMax[0].y > voxelFieldAABBMinMax[1].y ||
            capsuleAABBMinMax[1].y < voxelFieldAABBMinMax[0].y ||
            capsuleAABBMinMax[0].z > voxelFieldAABBMinMax[1].z ||
            capsuleAABBMinMax[1].z < voxelFieldAABBMinMax[0].z)
            return false;

        auto broadPhaseTimingDiff = std::chrono::high_resolution_clock::now() - broadPhaseTimingStart;

        //
        // Narrow phase: check all filled voxels within the capsule AABB
        //
        auto narrowPhaseTimingStart = std::chrono::high_resolution_clock::now();
        glm::ivec3 searchMin = glm::ivec3(
            glm::max(glm::floor(capsuleAABBMinMax[0].x), voxelFieldAABBMinMax[0].x),
            glm::max(glm::floor(capsuleAABBMinMax[0].y), voxelFieldAABBMinMax[0].y),
            glm::max(glm::floor(capsuleAABBMinMax[0].z), voxelFieldAABBMinMax[0].z)
        );
        glm::ivec3 searchMax = glm::ivec3(
            glm::min(glm::floor(capsuleAABBMinMax[1].x), voxelFieldAABBMinMax[1].x - 1),
            glm::min(glm::floor(capsuleAABBMinMax[1].y), voxelFieldAABBMinMax[1].y - 1),
            glm::min(glm::floor(capsuleAABBMinMax[1].z), voxelFieldAABBMinMax[1].z - 1)
        );

        bool collisionSuccessful = false;
        float_t lowestDpSqrDist = std::numeric_limits<float_t>::max();
        size_t lkjlkj = 0;
        size_t succs = 0;
        for (size_t i = searchMin.x; i <= searchMax.x; i++)
        for (size_t j = searchMin.y; j <= searchMax.y; j++)
        for (size_t k = searchMin.z; k <= searchMax.z; k++)
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
                    glm::vec3 point;
                    closestPointToLineSegment(glm::vec3(i, j, k) + glm::vec3(0.5f, 0.5f, 0.5f), capsulePtATransformed, capsulePtBTransformed, point);

                    glm::vec3 boundedPoint = glm::clamp(point, glm::vec3(i, j, k), glm::vec3(i + 1.0f, j + 1.0f, k + 1.0f));
                    if (point == boundedPoint)
                    {
                        // Collider is stuck inside
                        collisionNormal = glm::vec3(0, 1, 0);
                        penetrationDepth = 1.0f;
                        return true;
                    }
                    else
                    {
                        // Get more accurate point with the bounded point
                        glm::vec3 betterPoint;
                        closestPointToLineSegment(boundedPoint, capsulePtATransformed, capsulePtBTransformed, betterPoint);

                        glm::vec3 deltaPoint = betterPoint - boundedPoint;
                        float_t dpSqrDist = glm::length2(deltaPoint);
                        if (dpSqrDist < cpd.radius * cpd.radius && dpSqrDist < lowestDpSqrDist)
                        {
                            // Collision successful
                            succs++;
                            collisionSuccessful = true;
                            lowestDpSqrDist = dpSqrDist;
                            collisionNormal = glm::transpose(glm::inverse(glm::mat3(vfpd.transform))) * glm::normalize(deltaPoint);
                            penetrationDepth = cpd.radius - glm::sqrt(dpSqrDist);
                        }
                    }
                } break;
            }
        }

        auto narrowPhaseTimingDiff = std::chrono::high_resolution_clock::now() - narrowPhaseTimingStart;
        // std::cout << "collided: checks: " << lkjlkj << "\tsuccs: " << succs << "\ttime (broad): " << broadPhaseTimingDiff  << "\ttime (narrow): " << narrowPhaseTimingDiff << "\tisGround: " << (collisionNormal.y >= 0.707106665647) << "\tnormal: " << collisionNormal.x << ", " << collisionNormal.y << ", " << collisionNormal.z << "\tdepth: " << penetrationDepth << std::endl;

        return collisionSuccessful;
    }

    bool debugCheckCapsuleColliding(const CapsulePhysicsData& cpd, glm::vec3& collisionNormal, float_t& penetrationDepth)
    {
        glm::vec3 normal;
        float_t penDepth;

        for (size_t i = 0; i < numVFsCreated; i++)
        {
            size_t& index = voxelFieldIndices[i];
            if (checkCapsuleCollidingWithVoxelField(voxelFieldPool[index], cpd, normal, penDepth))
            {
                collisionNormal = normal;
                penetrationDepth = penDepth;
                return true;
            }
        }
        return false;
    }

    void moveCapsuleAccountingForCollision(CapsulePhysicsData& cpd, glm::vec3 deltaPosition, float_t ccdDistance)
    {
        do
        {
            if (glm::length2(deltaPosition) > ccdDistance * ccdDistance)
            {
                // Move at a max of the ccdDistance
                glm::vec3 m = glm::normalize(deltaPosition) * ccdDistance;
                cpd.basePosition += m;
                deltaPosition -= m;
            }
            else
            {
                // Move the rest of the way
                cpd.basePosition += deltaPosition;
                deltaPosition = glm::vec3(0.0f);
            }

            // Check for collision
            for (size_t iterations = 0; iterations < 6; iterations++)
            {
                glm::vec3 normal;
                float_t penetrationDepth;
                if (physengine::debugCheckCapsuleColliding(cpd, normal, penetrationDepth))
                {
                    penetrationDepth += 0.0001f;
                    if (normal.y >= 0.707106781187)  // >=45 degrees
                    {
                        // Stick to the ground
                        cpd.basePosition.y += penetrationDepth / normal.y;
                    }
                    else
                        cpd.basePosition += normal * penetrationDepth;
                }
                else
                    break;
            }
        } while (glm::length2(deltaPosition) > 0.000001f);
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
                glm::vec3 interpolPos = glm::mix(physutil::getPosition(vfpd.prevTransform), physutil::getPosition(vfpd.transform), physicsAlpha);
                glm::vec3 interpolSca = glm::mix(   physutil::getScale(vfpd.prevTransform),    physutil::getScale(vfpd.transform), physicsAlpha);

                glm::quat rotA = physutil::getRotation(vfpd.prevTransform);
                glm::quat rotB = physutil::getRotation(vfpd.transform);
                float_t omu = 1.0f - physicsAlpha;
                if (glm::dot(rotA, rotB) < 0.0f)  // Super simple neighboring... might be glitchy  @TODO
                    omu = -omu;
                glm::quat interpolRot = glm::normalize(omu * rotA + physicsAlpha * rotB);

                vfpd.interpolTransform =
                    glm::translate(glm::mat4(1.0f), interpolPos) *
                    glm::toMat4(interpolRot) *
                    glm::scale(glm::mat4(1.0f), interpolSca);
            }
        }
        for (size_t i = 0; i < numCapsCreated; i++)
        {
            CapsulePhysicsData& cpd = capsulePool[capsuleIndices[i]];
            if (cpd.prevBasePosition != cpd.basePosition)
            {
                cpd.interpolBasePosition = glm::mix(cpd.prevBasePosition, cpd.basePosition, physicsAlpha);
            }
        }
    }
}