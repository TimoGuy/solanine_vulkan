#pragma once

#include "Imports.h"
class PhysicsEngine;
class DataSerializer;
class DataSerialized;


namespace windmgr
{
    struct WindZone
    {
        glm::vec3 position    = { 0.0f, 0.0f, 0.0f };
        glm::quat rotation    = { 0.0f, 0.0f, 0.0f, 1.0f };
        glm::vec3 halfExtents = { 1.0f, 1.0f, 1.0f };
    };

    extern std::vector<WindZone> windZones;
    extern glm::vec3             windVelocity;  // Just has a global velocity... might change this in the future... depends I guess on what level designs I wanna make
    extern bool                  debugRenderCollisionDataFlag;
    void debugRenderCollisionData(PhysicsEngine* pe);
    void dumpWindZones(DataSerializer& ds);
    void loadWindZones(DataSerialized& ds);

    glm::vec3 getWindZoneVelocity(const glm::vec3& position);
}
