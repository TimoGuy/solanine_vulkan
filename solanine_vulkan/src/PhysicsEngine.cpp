#include "PhysicsEngine.h"

#include <iostream>


namespace physengine
{
    //
    // Voxel field pool
    //
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
                index = (voxelFieldIndices[numVFsCreated - 1] + 1) % PHYSICS_OBJECTS_MAX_CAPACITY;
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
                index = (capsuleIndices[numCapsCreated - 1] + 1) % PHYSICS_OBJECTS_MAX_CAPACITY;
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

        //
        // Narrow phase: check all filled voxels within the capsule AABB
        //
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
        for (size_t i = searchMin.x; i <= searchMax.x; i++)
        for (size_t j = searchMin.y; j <= searchMax.y; j++)
        for (size_t k = searchMin.z; k <= searchMax.z; k++)
        {
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
                    closestPointToLineSegment(glm::vec3(i + 0.5f, j + 0.5f, k + 0.5f), capsulePtATransformed, capsulePtBTransformed, point);
                    glm::vec3 boundedPoint = glm::clamp(point, glm::vec3(i, j, k), glm::vec3(i + 1.0f, j + 1.0f, k + 1.0f));
                    glm::vec3 deltaPoint = point - boundedPoint;
                    float_t dpSqrDist = glm::length2(deltaPoint);

                    if (dpSqrDist < 0.000001f)
                    {
                        // Collider is stuck
                        collisionSuccessful = true;
                        collisionNormal = glm::vec3(0, 1, 0);
                        penetrationDepth = 1.0f;
                    }
                    else if (dpSqrDist < cpd.radius * cpd.radius && dpSqrDist < lowestDpSqrDist)
                    {
                        // Collision successful
                        collisionSuccessful = true;
                        lowestDpSqrDist = dpSqrDist;
                        collisionNormal = glm::normalize(deltaPoint);
                        penetrationDepth = cpd.radius - glm::sqrt(dpSqrDist);
                    }
                } break;
            }
        }

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
}