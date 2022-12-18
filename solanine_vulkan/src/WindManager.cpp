#include "WindManager.h"

#include "PhysicsEngine.h"
#include "DataSerialization.h"


namespace windmgr
{
    std::vector<WindZone> windZones;
    glm::vec3             windVelocity = { 0, 0, 15 };
    float_t               windCheckRayLength = 30.0f;

    bool                  debugRenderCollisionDataFlag = true;


    void debugRenderCollisionData(PhysicsEngine* pe)
    {
        if (!debugRenderCollisionDataFlag)
            return;

        std::vector<glm::vec3> vertexList;

        for (auto& wz : windZones)
        {
            // @COPYPASTA
            glm::vec3 tempVertex;
            glm::mat4 transform =
                glm::translate(glm::mat4(1.0f), wz.position) *
                glm::toMat4(wz.rotation) *
                glm::scale(glm::mat4(1.0f), wz.halfExtents);

            tempVertex = glm::vec3(-1, 1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(1, 1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(1, 1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(-1, 1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(-1, 1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(-1, -1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(1, -1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(1, -1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(-1, -1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(-1, -1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            
            tempVertex = glm::vec3(-1, -1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(-1, 1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            
            tempVertex = glm::vec3(1, -1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(1, 1, -1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));

            tempVertex = glm::vec3(1, -1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
            tempVertex = glm::vec3(1, 1, 1);
            vertexList.push_back(transform * glm::vec4(tempVertex, 1.0f));
        }

        // Load all of them as a debug line
        static glm::vec3 color(1.0, 0.369, 0.369);
        for (size_t i = 0; i < vertexList.size(); i += 2)
            pe->debugDrawLineOneFrame(vertexList[i], vertexList[i + 1], color);
    }

    void dumpWindZones(DataSerializer& ds)
    {
        ds.dumpVec3(windVelocity);

        for (auto wz : windZones)
        {
            ds.dumpVec3(wz.position);
            ds.dumpQuat(wz.rotation);
            ds.dumpVec3(wz.halfExtents);
        }
    }

    void loadWindZones(DataSerialized& ds)
    {
        windVelocity = ds.loadVec3();

        windZones.clear();
        while (ds.getSerializedValuesCount() >= 3)
        {
            WindZone wz;
            wz.position    = ds.loadVec3();
            wz.rotation    = ds.loadQuat();
            wz.halfExtents = ds.loadVec3();
            windZones.push_back(wz);
        }
    }

    WZOState getWindZoneOccupancyState(const glm::vec3& position)
    {
        for (auto& wz : windZones)
        {
            glm::vec3 transformedPosition = position - wz.position;
            transformedPosition = glm::inverse(wz.rotation) * transformedPosition;
            transformedPosition = transformedPosition / wz.halfExtents;

            transformedPosition = glm::abs(transformedPosition);
            if (transformedPosition.x < 1.0f && transformedPosition.y < 1.0f && transformedPosition.z < 1.0f)
            {
                // In a wind zone. Check whether there is an obstruction
                bool windZoneOccluded = false;
                auto checkPos = physutil::toVec3(position);
                if (glm::length2(windVelocity) > 0.000001f)
                {
                    glm::vec3 wvGlmNormalized = glm::normalize(-windVelocity);
                    auto hitInfo = PhysicsEngine::getInstance().raycast(checkPos, checkPos + physutil::toVec3(wvGlmNormalized) * windCheckRayLength);
                    PhysicsEngine::getInstance().debugDrawLineOneFrame(physutil::toVec3(checkPos), physutil::toVec3(checkPos + physutil::toVec3(wvGlmNormalized) * windCheckRayLength),   glm::vec3(1, 0.5f, 1));

                    if (hitInfo.hasHit())
                        windZoneOccluded = true;
                }

                return windZoneOccluded ? WZOState::INSIDE_OCCLUDED : WZOState::INSIDE;
            }
        }

        return WZOState::NONE;
    }
}
