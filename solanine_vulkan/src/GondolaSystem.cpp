#include "GondolaSystem.h"

#include <Jolt/Jolt.h>
#include "EntityManager.h"
#include "RenderObject.h"
#include "PhysicsEngine.h"
#include "HotswapResources.h"
#include "SceneManagement.h"
#include "InputManager.h"
#include "GlobalState.h"
#include "DataSerialization.h"
#include "VoxelField.h"
#include "VulkanEngine.h"
#include "imgui/imgui.h"


VulkanEngine* GondolaSystem::_engine = nullptr;
std::mutex useControlPointsMutex;

constexpr float_t LENGTH_STATION_LOCAL_NETWORK = 110.0f;
constexpr float_t LENGTH_STATION_T_SPACE = 2.0f;  // Station spans 3 control points, so 2.0f in T-space.

constexpr size_t NUM_CARTS_LOCAL_NETWORK = 4;
constexpr float_t LENGTH_CART_LOCAL_NETWORK = 26.0f;
constexpr float_t MARGIN_CART_LOCAL_NETWORK = 1.0f;
constexpr float_t LENGTH_BOGIE_PADDING_LOCAL_NETWORK = LENGTH_CART_LOCAL_NETWORK / 6.5f;  // This is the measured proportion on the Japanese trains on Yamanote-sen.

struct GondolaSystem_XData
{
    VulkanEngine*              engineRef;
    RenderObjectManager*       rom;
    RenderObject*              controlRenderObj;

    vec3                       position = GLM_VEC3_ZERO_INIT;
    struct ControlPoint
    {
        vec3          position;
        RenderObject* renderObj;
    };
    std::vector<ControlPoint>  controlPoints;
    float_t                    lineYoff = 0.5f;  // Add this to the y of the rendered lines.
    float_t                    lineSeparation = 9.5f;  // Multiply `.right` by this.

    struct BSplineCoefficients
    {
        vec4 coefficientsX;
        vec4 coefficientsY;
        vec4 coefficientsZ;
    };
    struct FABCoefficients  // FAB=Forward and Backward
    {
        BSplineCoefficients rightSideCoefs;
        BSplineCoefficients leftSideCoefs;
    };
    std::vector<FABCoefficients> splineCoefficientsCache;
    struct TransitionalSpline
    {
        size_t cpIdxFrom;
        size_t cpIdxTo;
        std::vector<FABCoefficients> transCoefficientsCache;
    };
    std::vector<TransitionalSpline> transSplines;
    struct DEBUGCurveVisualization
    {
        struct PointSet
        {
            std::vector<vec3s> pts;
        };
        PointSet splineLinePts;
        std::vector<PointSet> curveLinePts;
    } DEBUGCurveVisualization;
    bool triggerBakeSplineCache = true;  // Do bake right at initialization.

    enum class GondolaNetworkType
    {
        FUTSUU,
        JUNKYUU,
        KAISOKU,
        TOKKYUU,
    };
    GondolaNetworkType gondolaNetworkType = GondolaNetworkType::FUTSUU;
    float_t gondolaSimulationGlobalTimer = 0.0f;

    struct TimeSlicing  // @NOTE: @TODO: as more things use timeslicing (if needed (ah but this one needs it I believe)), bring this out into a global counter.
    {
        size_t tickCount = 0;
        size_t position = 3;
        size_t total = 16;

        bool checkTimeslice()
        {
            return (tickCount++ % total == position);
        }
    } timeslicing;

    struct Simulation
    {
        float_t                    offsetT;
        std::vector<RenderObject*> renderObjs;  // @NOTE: For LODs, switch out the assigned model, not unregister/register new RenderObjects.  -Timo 2020/10/04
        struct GondolaCart
        {
            float_t length;  // Length of the cabin (excluding the connector halls)
            float_t frontMargin;  // Length of front connector hall
            float_t rearMargin;  // Length of rear connector hall
            float_t bogiePadding;  // Length from where the cabin starts (for bogie #1) and from where the cabin ends (for bogie #2) to put the bogies

            vec3 bogiePosition1;
            vec3 bogiePosition2;

            // Calculated values:
            vec3   calcCurrentROPos;
            versor calcCurrentRORot;
            vec3   calcPrevROPos;
            versor calcPrevRORot;
        };
        std::vector<GondolaCart> carts;  // Essentially metadata for the collision objects.
    };
    std::vector<Simulation> simulations;
    uint8_t numTrackVersions = 2;

    struct DetailedGondola
    {
        bool active = false;
        float_t priorityRange = 500.0f;  // This should be enough to support up to 16 cart long gondolas.
        size_t prevClosestSimulation = (size_t)-1;
        std::vector<VoxelField*> collisions;  // Collision objects for the most nearby train to the player character.
    } detailedGondola;

    struct Station
    {
        size_t anchorCPIdx = (size_t)-1;
        size_t secondaryForwardCPIdx = (size_t)-1;
        size_t secondaryBackwardCPIdx = (size_t)-1;
        size_t auxiliaryForwardCPIdx = (size_t)-1;
        size_t auxiliaryBackwardCPIdx = (size_t)-1;
        RenderObject* renderObj = nullptr;
    };
    std::vector<Station> stations;

    struct StationCommon
    {
        vec3 anchorCPLocalOffset = GLM_VEC3_ZERO_INIT;
        vec3 secondaryForwardCPLocalOffset = { 0.0f, 0.0f, -LENGTH_STATION_LOCAL_NETWORK * 0.5f };
        vec3 secondaryBackwardCPLocalOffset = { 0.0f, 0.0f, LENGTH_STATION_LOCAL_NETWORK * 0.5f };
        vec3 auxiliaryForwardCPLocalOffset = { 0.0f, 0.0f, -LENGTH_STATION_LOCAL_NETWORK };
        vec3 auxiliaryBackwardCPLocalOffset = { 0.0f, 0.0f, LENGTH_STATION_LOCAL_NETWORK };
    } stationCommon;

    struct DetailedStation
    {
        float_t priorityRange = 5000.0f;
        size_t  prevClosestStation = (size_t)-1;
        VoxelField* collision = nullptr;  // Only support 1 type of station (FOR NOW), so don't load out the collision when exiting the priorityRange.  -Timo 2023/10/05
    } detailedStation;
};

constexpr float_t ENTER_LEAVE_STATION_TIME = 3.0f;  // https://www.desmos.com/calculator/emqtb8qoby
constexpr float_t WAIT_AT_STATION_TIME = 5.0f;

float_t getTotalSimulationLength(GondolaSystem_XData* d)
{
    float_t numLines = (float_t)d->controlPoints.size() - 1.0f;
    float_t excludedSpace = LENGTH_STATION_T_SPACE;  // This adds up to the station length (padding before the train plus the train length up to the leading bogie in the beginning, then at the end from the leading bogie to the very end of the spline, so this ends up just being the length of the station itself, aka 2.0f).  -Timo 2023/10/06
    float_t total =
        numLines - excludedSpace +
        (float_t)(d->stations.size() - 1) * (
            (ENTER_LEAVE_STATION_TIME - 1.0f) * 2.0f +
            WAIT_AT_STATION_TIME
        ) - 0.000001f;
    return total * 2.0f;  // The `*2` at the end is bc the simulation has the pong of the ping-ponging.
}

float_t getTotalGondolaLength()
{
    return LENGTH_CART_LOCAL_NETWORK * NUM_CARTS_LOCAL_NETWORK +
        MARGIN_CART_LOCAL_NETWORK * (NUM_CARTS_LOCAL_NETWORK * 2.0f - 2.0f);  // The `- 2.0f` is for the margin not for the front and back of the end carts.
}

float_t getNextStationArriveStartT(GondolaSystem_XData* d, float_t t, bool reverseRoute)
{
    float_t tTargetStationArrivalOffset =
        (getTotalGondolaLength() * 0.5f - LENGTH_BOGIE_PADDING_LOCAL_NETWORK) / LENGTH_STATION_LOCAL_NETWORK * LENGTH_STATION_T_SPACE;

    float_t nextTTarget = -1.0f;
    float_t lowestDiff = std::numeric_limits<float_t>::max();

    int64_t i = (reverseRoute ? d->stations.size() - 1 : 0.0f);
    for (;;)
    {
        float_t tTarget = (float_t)d->stations[i].anchorCPIdx + tTargetStationArrivalOffset * (reverseRoute ? -1.0f : 1.0f);
        float_t diff = (tTarget - t) * (reverseRoute ? -1.0f : 1.0f);
        if (diff > 0.0f && diff < lowestDiff)
        {
            nextTTarget = tTarget;
            lowestDiff = diff;
        }

        // Increment.
        if (reverseRoute)
        {
            i--;
            if (i < 0)
                break;
        }
        else
        {
            i++;
            if (i >= d->stations.size())
                break;
        }
    }

    return nextTTarget;
}

void getTFromSimulationTime(GondolaSystem_XData* d, float_t time, float_t& outT, bool& reverseRoute)
{
    float_t totalSimLength = getTotalSimulationLength(d);
    while (time < 0.0f)
        time += totalSimLength;
    while (time >= totalSimLength)
        time -= totalSimLength;

    // Chop up simulation into pieces and see what occurred.
    float_t tDistFromEndOfStationToBeginningOfGondola = (LENGTH_STATION_LOCAL_NETWORK - getTotalGondolaLength()) * 0.5f / LENGTH_STATION_LOCAL_NETWORK * LENGTH_STATION_T_SPACE;
    float_t tLengthOfGondolaUpToLeadingBogie = (getTotalGondolaLength() - LENGTH_BOGIE_PADDING_LOCAL_NETWORK) / LENGTH_STATION_LOCAL_NETWORK * LENGTH_STATION_T_SPACE;

    if (time < totalSimLength * 0.5f)
    {
        outT = tDistFromEndOfStationToBeginningOfGondola + tLengthOfGondolaUpToLeadingBogie;  // Start where the first bogie would be placed.
        reverseRoute = false;
    }
    else
    {
        outT = (float_t)d->controlPoints.size() - 1.000001f - (tDistFromEndOfStationToBeginningOfGondola + tLengthOfGondolaUpToLeadingBogie);
        reverseRoute = true;
        time -= totalSimLength * 0.5f;
    }
    
    // Step thru simulation.
    enum class SimulationStage { TRAVEL, LEAVE_STATION, ARRIVE_INTO_STATION }
        currentSimulationStage = SimulationStage::LEAVE_STATION;
    while (time > 0.0f)
    {
        switch (currentSimulationStage)
        {
            case SimulationStage::TRAVEL:
            {
                // Check if can move for the whole time.
                float_t nextT = getNextStationArriveStartT(d, outT, reverseRoute);
                if (nextT < 0.0f)
                {
                    // No new station is found, just go to the end.
                    nextT = (reverseRoute ? 0.0f : (float_t)d->controlPoints.size() - 1.000001f);
                }

                float_t maxAbleToMove = (nextT - outT) * (reverseRoute ? -1.0f : 1.0f) - 1.0f;  // `- 1.0f` is for the space in T-space the simulation moves before it arrives in the station.
                if (time < maxAbleToMove)
                {
                    // Move for the whole time.
                    outT += time * (reverseRoute ? -1.0f : 1.0f);
                    time = 0.0f;
                }
                else
                {
                    // Move just the max, then switch to `ARRIVE_INTO_STATION`.
                    outT += maxAbleToMove * (reverseRoute ? -1.0f : 1.0f);
                    time -= maxAbleToMove;
                    currentSimulationStage = SimulationStage::ARRIVE_INTO_STATION;
                }
            } break;

            case SimulationStage::LEAVE_STATION:
            {
                if (time >= ENTER_LEAVE_STATION_TIME)
                    outT += 1.0f * (reverseRoute ? -1.0f : 1.0f);
                else
                    outT += std::powf(time / ENTER_LEAVE_STATION_TIME, ENTER_LEAVE_STATION_TIME) * (reverseRoute ? -1.0f : 1.0f);
                time -= ENTER_LEAVE_STATION_TIME;
                currentSimulationStage = SimulationStage::TRAVEL;
            } break;

            case SimulationStage::ARRIVE_INTO_STATION:
            {
                if (time >= ENTER_LEAVE_STATION_TIME)
                    outT += 1.0f * (reverseRoute ? -1.0f : 1.0f);
                else
                    outT += (-std::powf((-time + ENTER_LEAVE_STATION_TIME) / ENTER_LEAVE_STATION_TIME, ENTER_LEAVE_STATION_TIME) + 1.0f) * (reverseRoute ? -1.0f : 1.0f);  // https://www.desmos.com/calculator/8r16nz38wb
                time -= ENTER_LEAVE_STATION_TIME;
                time -= WAIT_AT_STATION_TIME;  // Wait at the station.
                currentSimulationStage = SimulationStage::LEAVE_STATION;
            } break;
        }
    }
}

void destructAndResetGondolaCollisions(GondolaSystem_XData* d, EntityManager* em)
{
    for (auto& collision : d->detailedGondola.collisions)
        em->destroyOwnedEntity((Entity*)collision);
    d->detailedGondola.collisions.clear();
}

void buildCollisions(GondolaSystem_XData* d, VulkanEngine* engineRef, std::vector<VoxelField*>& outCollisions, GondolaSystem_XData::GondolaNetworkType networkType)
{
    // Load prefab from file.
    std::vector<Entity*> ents;
    switch (networkType)
    {
        case GondolaSystem_XData::GondolaNetworkType::FUTSUU:
            scene::loadPrefab("gondola_collision_futsuu.hunk", engineRef, ents);  // Supposed to contain all the cars for collision (even though there are repeats in the collision data).
            break;

        case GondolaSystem_XData::GondolaNetworkType::JUNKYUU:
            scene::loadPrefab("gondola_collision_junkyuu.hunk", engineRef, ents);
            break;

        case GondolaSystem_XData::GondolaNetworkType::KAISOKU:
            scene::loadPrefab("gondola_collision_kaisoku.hunk", engineRef, ents);
            break;

        case GondolaSystem_XData::GondolaNetworkType::TOKKYUU:
            scene::loadPrefab("gondola_collision_tokkyuu.hunk", engineRef, ents);
            break;
    }

    // Cast prefab contents into VoxelFields.
    for (auto& ent : ents)
    {
        VoxelField* entAsVF;
        if (entAsVF = dynamic_cast<VoxelField*>(ent))
        {
            entAsVF->setBodyKinematic(true);  // Since they'll be essentially glued from the track, no use having them be dynamic.
            outCollisions.push_back(entAsVF);
        }
    }
}

void moveCollisionBodies(GondolaSystem_XData* d, bool staticMove, float_t physicsDeltaTime)  // If `staticMove==false`, then will be kinematic move.
{
    for (size_t i = 0; i < d->detailedGondola.collisions.size(); i++)
    {
        auto& collision = d->detailedGondola.collisions[i];
        auto& cart = d->simulations[d->detailedGondola.prevClosestSimulation].carts[i];

        vec3 extent;
        collision->getSize(extent);
        glm_vec3_scale(extent, -0.5f, extent);
        mat3 rotation;
        glm_quat_mat3(cart.calcCurrentRORot, rotation);
        glm_mat3_mulv(rotation, extent, extent);
        vec3 newPos;
        glm_vec3_add(cart.calcCurrentROPos, extent, newPos);

        collision->moveBody(newPos, cart.calcCurrentRORot, staticMove, physicsDeltaTime);
    }
}

void readyGondolaInteraction(GondolaSystem_XData* d, EntityManager* em, const GondolaSystem_XData::Simulation& simulation, size_t desiredSimulationIdx)
{
    // Clear and rebuild
    destructAndResetGondolaCollisions(d, em);
    buildCollisions(d, d->engineRef, d->detailedGondola.collisions, d->gondolaNetworkType);
    d->detailedGondola.prevClosestSimulation = desiredSimulationIdx;  // Mark cache as completed.
    moveCollisionBodies(d, true, 0.0f);
}

void destructAndResetStationCollision(GondolaSystem_XData* d, EntityManager* em)
{
    em->destroyOwnedEntity((Entity*)d->detailedStation.collision);
    d->detailedStation.collision = nullptr;
    d->detailedStation.prevClosestStation = (size_t)-1;
}

void readyStationInteraction(GondolaSystem_XData* d, GondolaSystem_XData::Station& station, size_t stationIdx)
{
    // Add station collision if not existing yet.
    if (d->detailedStation.collision == nullptr)
    {
        std::vector<Entity*> ents;
        scene::loadPrefab("gondola_collision_station.hunk", d->engineRef, ents);
        for (auto& ent : ents)
        {
            VoxelField* entAsVF;
            if (entAsVF = dynamic_cast<VoxelField*>(ent))
            {
                // Just nab the first voxelfield and then dip.
                d->detailedStation.collision = entAsVF;
                break;
            }
        }
    }

    // Calc the station direction using the secondary points.
    vec3 delta;
    glm_vec3_sub(
        d->controlPoints[station.secondaryForwardCPIdx].position,
        d->controlPoints[station.secondaryBackwardCPIdx].position,
        delta
    );
    glm_vec3_scale_as(delta, LENGTH_STATION_LOCAL_NETWORK * 0.5f, delta);

    // Move the station transform to there.
    // @NOTE: this should only be executed when setting the collision to the station position, or creating a new station collision.
    float_t yRot = std::atan2f(delta[0], delta[2]) + M_PI;
    float_t xzDist = glm_vec2_norm(vec2{ delta[0], delta[2] });
    float_t xRot = std::atan2f(delta[1], xzDist);
    mat4 rotation;
    glm_euler_zyx(vec3{ xRot, yRot, 0.0f }, rotation);
    versor rotationV;
    glm_mat4_quat(rotation, rotationV);

    vec3 extent;
    d->detailedStation.collision->getSize(extent);
    glm_vec3_scale(extent, -0.5f, extent);
    glm_mat4_mulv3(rotation, extent, 0.0f, extent);
    vec3 newPos;
    glm_vec3_add(d->controlPoints[station.anchorCPIdx].position, extent, newPos);

    d->detailedStation.collision->moveBody(newPos, rotationV, true, 0.0f);
    d->detailedStation.prevClosestStation = stationIdx;
    // glm_mat4_zero(d->detailedStation.prevCollisionTransform);  // Invalidate prev collision cache.  // @INCOMPLETE: there's no way to tell if the new transform is similar to the old one.
}

void loadAllMissingStationRenderObjs(GondolaSystem_XData* d, GondolaSystem* _this, const std::string& myGuid)
{
    for (auto& station : d->stations)
    {
        if (station.renderObj != nullptr)
            continue;

        d->rom->registerRenderObjects({
                {
                    .model = d->rom->getModel("BuilderObj_GondolaStation", _this, [](){}),
                    .renderLayer = RenderLayer::BUILDER,
                    .attachedEntityGuid = myGuid,
                }
            },
            { &station.renderObj }
        );

        // Find rotation to set the render obj.
        vec3 delta;
        glm_vec3_sub(
            d->controlPoints[station.secondaryForwardCPIdx].position,
            d->controlPoints[station.secondaryBackwardCPIdx].position,
            delta
        );
        float_t yRot = std::atan2f(delta[0], delta[2]) + M_PI;
        float_t xzDist = glm_vec2_norm(vec2{ delta[0], delta[2] });
        float_t xRot = std::atan2f(delta[1], xzDist);
        mat4 rotation;
        glm_euler_zyx(vec3{ xRot, yRot, 0.0f }, rotation);

        // Construct transform.
        glm_mat4_identity(station.renderObj->transformMatrix);
        glm_translate(station.renderObj->transformMatrix, d->controlPoints[station.anchorCPIdx].position);
        glm_mul_rot(station.renderObj->transformMatrix, rotation, station.renderObj->transformMatrix);
    }
}

void calculateStationSecAuxCPIndices(GondolaSystem_XData* d)
{
    for (auto& station : d->stations)
    {
        size_t acp = station.anchorCPIdx;
        station.secondaryForwardCPIdx = acp + 1;
        station.secondaryBackwardCPIdx = acp - 1;
        if (acp < d->controlPoints.size() - 2)
            station.auxiliaryForwardCPIdx = acp + 2;
        if (acp > 1)
            station.auxiliaryBackwardCPIdx = acp - 2;
    }
}

GondolaSystem::GondolaSystem(EntityManager* em, RenderObjectManager* rom, VulkanEngine* engineRef, DataSerialized* ds) : Entity(em, ds), _data(new GondolaSystem_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;
    _data->engineRef = engineRef;

    if (ds)
        load(*ds);

    // Control Obj render obj.
    _data->rom->registerRenderObjects({
            {
                .model = _data->rom->getModel("BuilderObj_GondolaControlObject", this, [](){}),
                .renderLayer = RenderLayer::BUILDER,
                .attachedEntityGuid = getGUID(),
            }
        },
        { &_data->controlRenderObj }
    );
    glm_translate(_data->controlRenderObj->transformMatrix, _data->position);

    // Initialize some control points.
    if (_data->controlPoints.empty())
    {
        _data->controlPoints.resize(4, {});
        for (size_t i = 0; i < _data->controlPoints.size(); i++)
        {
            auto& cp = _data->controlPoints[i];
            glm_vec3_add(_data->position, vec3{ 0.0f, -5.0f, (float_t)i }, cp.position);
        }
    }

    // Load in control point render objs.
    std::vector<RenderObject> inROs;
    std::vector<RenderObject**> outROs;
    inROs.resize(_data->controlPoints.size(), {
        .model = _data->rom->getModel("BuilderObj_BezierHandle", this, [](){}),
        .renderLayer = RenderLayer::BUILDER,
        .attachedEntityGuid = getGUID(),
    });
    for (auto& cp : _data->controlPoints)
        outROs.push_back(&cp.renderObj);
    _data->rom->registerRenderObjects(inROs, outROs);

    // Set up stations.
    calculateStationSecAuxCPIndices(_data);
    loadAllMissingStationRenderObjs(_data, this, getGUID());
}

GondolaSystem::~GondolaSystem()
{
    hotswapres::removeOwnedCallbacks(this);

    std::vector<RenderObject*> renderObjsToUnregister;
    renderObjsToUnregister.push_back(_data->controlRenderObj);
    for (auto& cp : _data->controlPoints)
        renderObjsToUnregister.push_back(cp.renderObj);
    for (auto& sim : _data->simulations)
        for (auto& ro : sim.renderObjs)
            renderObjsToUnregister.push_back(ro);
    for (auto& stn : _data->stations)
        if (stn.renderObj != nullptr)
            renderObjsToUnregister.push_back(stn.renderObj);
    _data->rom->unregisterRenderObjects(renderObjsToUnregister);

    destructAndResetGondolaCollisions(_data, _em);

    if (_data->detailedStation.collision != nullptr)
    {
        destructAndResetStationCollision(_data, _em);
    }

    delete _data;
}

void calculateSplineCoefficient(float_t p0, float_t p1, float_t p2, float_t p3, vec4& outCoefficient)
{
    constexpr float_t mult = 1.0f / 6.0f;
    outCoefficient[0] = mult * (        p0 + 4.0f * p1 +        p2);
    outCoefficient[1] = mult * (-3.0f * p0 +             3.0f * p2);
    outCoefficient[2] = mult * ( 3.0f * p0 - 6.0f * p1 + 3.0f * p2);
    outCoefficient[3] = mult * (-1.0f * p0 + 3.0f * p1 - 3.0f * p2 + p3);
}

void calculateSplineCoefficients(vec3 p0, vec3 p1, vec3 p2, vec3 p3, GondolaSystem_XData::BSplineCoefficients& outBSC)
{
    calculateSplineCoefficient(p0[0], p1[0], p2[0], p3[0], outBSC.coefficientsX);
    calculateSplineCoefficient(p0[1], p1[1], p2[1], p3[1], outBSC.coefficientsY);
    calculateSplineCoefficient(p0[2], p1[2], p2[2], p3[2], outBSC.coefficientsZ);
}

void getPositionFromTAndCoefficients(GondolaSystem_XData::BSplineCoefficients& bsc, float_t t, vec3& outPosition)
{
    vec4 tInputs = {
        1.0f,
        t,
        t * t,
        t * t * t
    };

    outPosition[0] = glm_vec4_dot(tInputs, bsc.coefficientsX);
    outPosition[1] = glm_vec4_dot(tInputs, bsc.coefficientsY);
    outPosition[2] = glm_vec4_dot(tInputs, bsc.coefficientsZ);
}

bool calculatePositionOnCurveFromT(GondolaSystem_XData* d, float_t t, vec3& outPosition, bool reverseRoute, uint8_t trackVersion)
{
    float_t wholeT, remainderT;
    remainderT = std::modf(t, &wholeT);

    if (t < 0.0f ||
        (size_t)wholeT >= d->splineCoefficientsCache.size())
        return false;  // Return false bc out of range.

    auto& coefficients = d->splineCoefficientsCache[(size_t)wholeT];

    bool useTransitionTrack = (reverseRoute == (bool)trackVersion);    // See canary pad. Lol.  -Timo 2023/10/10
    GondolaSystem_XData::BSplineCoefficients* coefs = nullptr;
    if (d->transSplines.empty() ||  // @NOTE: Assume there can only be 2 end stations.
        (d->transSplines[0].cpIdxTo <= (size_t)wholeT && (size_t)wholeT < d->transSplines[1].cpIdxFrom))                           // Within normal functioning part of rail.
        coefs = (reverseRoute ? &coefficients.leftSideCoefs : &coefficients.rightSideCoefs);
    else if (useTransitionTrack && d->transSplines[0].cpIdxFrom <= (size_t)wholeT && (size_t)wholeT < d->transSplines[0].cpIdxTo)  // Within transition boundary of 1st end station.
    {
        auto& tcc = d->transSplines[0].transCoefficientsCache[(size_t)wholeT - d->transSplines[0].cpIdxFrom];
        coefs = (reverseRoute ? &tcc.rightSideCoefs : &tcc.leftSideCoefs);
    }
    else if (useTransitionTrack && d->transSplines[1].cpIdxFrom <= (size_t)wholeT && (size_t)wholeT < d->transSplines[1].cpIdxTo)  // Within transition boundary of 2nd end station.
    {
        auto& tcc = d->transSplines[1].transCoefficientsCache[(size_t)wholeT - d->transSplines[1].cpIdxFrom];
        coefs = (reverseRoute ? &tcc.leftSideCoefs : &tcc.rightSideCoefs);
    }
    else                                                                                                                           // Within end stations.
        coefs = (trackVersion == 0 ? &coefficients.leftSideCoefs : &coefficients.rightSideCoefs);

    getPositionFromTAndCoefficients(*coefs, remainderT, outPosition);
    return true;
}

void drawDEBUGCurveVisualization(GondolaSystem_XData* d)
{
    for (size_t i = 1; i < d->DEBUGCurveVisualization.splineLinePts.pts.size(); i++)
        physengine::drawDebugVisLine(
            d->DEBUGCurveVisualization.splineLinePts.pts[i - 1].raw,
            d->DEBUGCurveVisualization.splineLinePts.pts[i].raw,
            physengine::DebugVisLineType::KIKKOARMY
        );
    for (auto& pointSet : d->DEBUGCurveVisualization.curveLinePts)
    for (size_t i = 1; i < pointSet.pts.size(); i++)
        physengine::drawDebugVisLine(
            pointSet.pts[i - 1].raw,
            pointSet.pts[i].raw,
            physengine::DebugVisLineType::PURPTEAL
        );
}

void spawnSimulation(GondolaSystem_XData* d, void* modelOwner, const std::string& guid, float_t offsetT)
{
    // Register cart render objects.
    GondolaSystem_XData::Simulation newSimulation;
    newSimulation.offsetT = offsetT;
    newSimulation.renderObjs.resize(NUM_CARTS_LOCAL_NETWORK, nullptr);

    std::vector<RenderObject> inROs;
    std::vector<RenderObject**> outROs;
    for (size_t i = 0; i < NUM_CARTS_LOCAL_NETWORK; i++)
    {
        // Setup render object registration.
        inROs.push_back({
            .model = d->rom->getModel("BuilderObj_GondolaNetworkFutsuu", modelOwner, [](){}),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = guid,
        });
        outROs.push_back(&newSimulation.renderObjs[i]);

        // Insert cart metadata.
        float_t frontMargin = (i == 0 ? 0.0f : MARGIN_CART_LOCAL_NETWORK);
        float_t rearMargin = (i == NUM_CARTS_LOCAL_NETWORK - 1 ? 0.0f : MARGIN_CART_LOCAL_NETWORK);
        GondolaSystem_XData::Simulation::GondolaCart newCart = {
            .length = LENGTH_CART_LOCAL_NETWORK,
            .frontMargin = frontMargin,
            .rearMargin = rearMargin,
            .bogiePadding = LENGTH_BOGIE_PADDING_LOCAL_NETWORK,
        };
        newSimulation.carts.push_back(newCart);
    }
    d->rom->registerRenderObjects(inROs, outROs);
    d->simulations.push_back(newSimulation);
}

bool searchForRightTOnCurve(GondolaSystem_XData* d, float_t& ioT, vec3 anchorPos, float_t targetDistance, bool reverseRoute, uint8_t trackVersion)
{
    float_t targetDistance2 = targetDistance * targetDistance;

    float_t searchStride = 0.5f;
    float_t searchDirection = (reverseRoute ? 1.0f : -1.0f);
    float_t searchPosDistWS2 = std::numeric_limits<float_t>::max();
    float_t maxT = (float_t)d->splineCoefficientsCache.size() - 0.000001f;

    while (std::abs(targetDistance2 - searchPosDistWS2) > 0.1f)  // Idk how much to do this to reduce jitter.
    {
        ioT += searchStride * searchDirection;
        
        bool maybeWantingToGoFurtherIntoUndefined = false;
        if (ioT < 0.0f)
        {
            ioT = 0.0f;
            maybeWantingToGoFurtherIntoUndefined = true;
        }
        if (ioT > maxT)
        {
            ioT = maxT;
            maybeWantingToGoFurtherIntoUndefined = true;
        }

        vec3 searchPosition;
        calculatePositionOnCurveFromT(d, ioT, searchPosition, reverseRoute, trackVersion);

        searchPosDistWS2 = glm_vec3_distance2(anchorPos, searchPosition);

        if (searchPosDistWS2 > targetDistance2)
        {
            if (searchDirection * (reverseRoute ? -1.0f : 1.0f) < 0.0f)
                searchStride *= 0.5f;  // Cut stride in half since crossed the target pos.
            else if (maybeWantingToGoFurtherIntoUndefined)
                return false;  // Doesn't want to turn around after dipping into undefined zone. Exit.
            searchDirection = (reverseRoute ? -1.0f : 1.0f);
        }
        else
        {
            if (searchDirection * (reverseRoute ? -1.0f : 1.0f) > 0.0f)
                searchStride *= 0.5f;  // Cut stride in half since crossed the target pos.
            else if (maybeWantingToGoFurtherIntoUndefined)
                return false;  // Doesn't want to turn around after dipping into undefined zone. Exit.
            searchDirection = (reverseRoute ? 1.0f : -1.0f);
        }
    }

    return true;
}

void updateSimulation(GondolaSystem_XData* d, EntityManager* em, size_t simIdx, const float& physicsDeltaTime)
{
    GondolaSystem_XData::Simulation& ioSimulation = d->simulations[simIdx];
    uint8_t trackVersion = (simIdx + 1) % d->numTrackVersions;

    // Position each render object position based off the position of the bogies.
    float_t currentPosT;
    bool reverseRoute;
    getTFromSimulationTime(d, d->gondolaSimulationGlobalTimer + ioSimulation.offsetT, currentPosT, reverseRoute);

    bool first = true;
    for (size_t rawI = 0; rawI < ioSimulation.carts.size(); rawI++)
    {
        size_t i = (reverseRoute ? ioSimulation.carts.size() - 1 - rawI : rawI);
        auto& cart = ioSimulation.carts[i];

        // Move to first bogie position.
        if (!first)
        {
            auto& prevCart = ioSimulation.carts[reverseRoute ? i + 1 : i - 1];
            float_t distanceToNext = prevCart.bogiePadding + prevCart.rearMargin + cart.frontMargin + cart.bogiePadding;
            searchForRightTOnCurve(d, currentPosT, prevCart.bogiePosition2, distanceToNext, reverseRoute, trackVersion);
        }

        if (!calculatePositionOnCurveFromT(d, currentPosT, cart.bogiePosition1, reverseRoute, trackVersion))
        {
            // // Remove simulation if out of range.
            // // @NOTE: @INCOMPLETE: this shouldn't happen. At the beginning there should be X number of gondolas spawned and then they all go in a uniform loop.
            // if (d->detailedGondola.prevClosestSimulation == simIdx)
            //     destructAndResetGondolaCollisions(d, em);
            // d->rom->unregisterRenderObjects(ioSimulation.renderObjs);
            // d->simulations.erase(d->simulations.begin() + simIdx);
            // d->detailedGondola.prevClosestSimulation = (size_t)-1;  // Invalidate detailedGondola collision cache.
        }

        // Move to second bogie position.
        float_t distanceToSecond = cart.length - 2.0f * cart.bogiePadding;
        searchForRightTOnCurve(d, currentPosT, cart.bogiePosition1, distanceToSecond, reverseRoute, trackVersion);

        calculatePositionOnCurveFromT(d, currentPosT, cart.bogiePosition2, reverseRoute, trackVersion);

        // Create new transform.
        glm_vec3_copy(cart.calcCurrentROPos, cart.calcPrevROPos);
        glm_quat_copy(cart.calcCurrentRORot, cart.calcPrevRORot);

        glm_vec3_add(cart.bogiePosition1, cart.bogiePosition2, cart.calcCurrentROPos);
        glm_vec3_scale(cart.calcCurrentROPos, 0.5f, cart.calcCurrentROPos);

        vec3 delta;
        if (reverseRoute)
            glm_vec3_sub(cart.bogiePosition2, cart.bogiePosition1, delta);
        else
            glm_vec3_sub(cart.bogiePosition1, cart.bogiePosition2, delta);
        float_t yRot = std::atan2f(delta[0], delta[2]) + M_PI;
        
        float_t xzDist = glm_vec2_norm(vec2{ delta[0], delta[2] });
        float_t xRot = std::atan2f(delta[1], xzDist);

        mat4 rotation;
        glm_euler_zyx(vec3{ xRot, yRot, 0.0f }, rotation);
        glm_mat4_quat(rotation, cart.calcCurrentRORot);

        first = false;
    }

    // Update physics objects.
    moveCollisionBodies(d, false, physicsDeltaTime);
}

void updateControlPointPositions(GondolaSystem_XData* d, GondolaSystem_XData::Station& ioStation, mat4 transform)
{
    glm_mat4_mulv3(transform, d->stationCommon.anchorCPLocalOffset, 1.0f, d->controlPoints[ioStation.anchorCPIdx].position);
    glm_mat4_mulv3(transform, d->stationCommon.secondaryForwardCPLocalOffset, 1.0f, d->controlPoints[ioStation.secondaryForwardCPIdx].position);
    glm_mat4_mulv3(transform, d->stationCommon.secondaryBackwardCPLocalOffset, 1.0f, d->controlPoints[ioStation.secondaryBackwardCPIdx].position);
    if (ioStation.auxiliaryForwardCPIdx != (size_t)-1)
        glm_mat4_mulv3(transform, d->stationCommon.auxiliaryForwardCPLocalOffset, 1.0f, d->controlPoints[ioStation.auxiliaryForwardCPIdx].position);
    if (ioStation.auxiliaryBackwardCPIdx != (size_t)-1)
        glm_mat4_mulv3(transform, d->stationCommon.auxiliaryBackwardCPLocalOffset, 1.0f, d->controlPoints[ioStation.auxiliaryBackwardCPIdx].position);
}

void updateStation(GondolaSystem_XData* d)
{
    auto& station = d->stations[d->detailedStation.prevClosestStation];
    mat4 transform;
    d->detailedStation.collision->getTransform(transform);

    // if (glm_mat4_similar(d->detailedStation.prevCollisionTransform, transform))  // @INCOMPLETE: there's currently no way to tell if the new transform is similar to the old one.
    //     return;

    vec3 extent;
    d->detailedStation.collision->getSize(extent);
    glm_vec3_scale(extent, 0.5f, extent);
    glm_translate(transform, extent);

    updateControlPointPositions(d, station, transform);
    
    d->triggerBakeSplineCache = true;  // Assumption is that the station moved.
}

bool showCurvePaths = true;

void GondolaSystem::physicsUpdate(const float_t& physicsDeltaTime)
{
    if (showCurvePaths)
        drawDEBUGCurveVisualization(_data);

    // Update simulations.
    // @TODO: there should be some kind of timeslicing for this, then update the timestamp of the new calculated point, and in `lateUpdate()` interpolate between the two generated points.
    // @REPLY: and then for the closest iterating one, there should be `updateSimulation` running every frame, since this affects the collisions too.
    //         In order to accomplish this, having a global timer is important. Then that way each timesliced gondola won't have to guess the timing and then it gets off.
    //         Just pass in the global timer value instead of `physicsDeltaTime`.  @TODO
    _data->gondolaSimulationGlobalTimer += physicsDeltaTime;
    for (int64_t i = _data->simulations.size() - 1; i >= 0; i--)  // Reverse iteration so that delete can happen.
        updateSimulation(_data, _em, i, physicsDeltaTime);

    if (_data->detailedStation.collision != nullptr &&
        _data->detailedStation.prevClosestStation != (size_t)-1)
        updateStation(_data);

    if (!_data->timeslicing.checkTimeslice())
        return;  // Exit bc not timesliced position.

    // Rebake curve visualization.
    if (_data->triggerBakeSplineCache)
    {
        std::lock_guard<std::mutex> lg(useControlPointsMutex);

        // Create transitional splines (if end stations exist).
        _data->transSplines.clear();
        uint8_t numEndStations = 0;
        size_t endStationAnchorIdxs[2];
        for (auto& station : _data->stations)
        {
            if (station.anchorCPIdx == 1 ||
                station.anchorCPIdx == _data->controlPoints.size() - 2)
                endStationAnchorIdxs[numEndStations++] = station.anchorCPIdx;
        }
        if (numEndStations == 2)
        {
            // Create transitional splines.
            _data->transSplines.resize(2, {});

            _data->transSplines[0].cpIdxFrom = endStationAnchorIdxs[0] + 1;
            _data->transSplines[0].cpIdxTo = endStationAnchorIdxs[0] + 4;
            _data->transSplines[1].cpIdxFrom = endStationAnchorIdxs[1] - 4;
            _data->transSplines[1].cpIdxTo = endStationAnchorIdxs[1] - 1;
        }

        // Calculate coefficient cache.
        _data->splineCoefficientsCache.clear();

        std::vector<vec3s> rights;
        rights.resize(_data->controlPoints.size());

        for (uint8_t calcType = 0; calcType < 2; calcType++)  // 0: calc rights  1: calc coefficients
        for (size_t i = 0; i < _data->controlPoints.size() - 1; i++)
        {
            vec3 p0, p1, p2, p3;
            vec3 r0, r1, r2, r3;
            glm_vec3_copy(_data->controlPoints[i].position, p1);
            glm_vec3_copy(rights[i].raw, r1);
            glm_vec3_copy(_data->controlPoints[i + 1].position, p2);
            glm_vec3_copy(rights[i + 1].raw, r2);

            if (i == 0)
            {
                // First point (mirror 2nd control point over 1st control point to get -1st control point).
                vec3 inter;
                glm_vec3_sub(p1, p2, inter);
                glm_vec3_add(inter, p1, p0);
                glm_vec3_copy(r1, r0);
            }
            else
            {
                glm_vec3_copy(_data->controlPoints[i - 1].position, p0);
                glm_vec3_copy(rights[i - 1].raw, r0);
            }

            if (i == _data->controlPoints.size() - 2)
            {
                // Last point (mirror 2nd to last control point over last control point to get ghost point).
                vec3 inter;
                glm_vec3_sub(p2, p1, inter);
                glm_vec3_add(inter, p2, p3);
                glm_vec3_copy(r2, r3);
            }
            else
            {
                glm_vec3_copy(_data->controlPoints[i + 2].position, p3);
                glm_vec3_copy(rights[i + 2].raw, r3);
            }

            vec3 offset = { 0.0f, _data->lineYoff, 0.0f };
            glm_vec3_add(p0, offset, p0);
            glm_vec3_add(p1, offset, p1);
            glm_vec3_add(p2, offset, p2);
            glm_vec3_add(p3, offset, p3);

            if (calcType == 0)
            {
                // Calc rights.
                GondolaSystem_XData::BSplineCoefficients tempCoeffs;
                calculateSplineCoefficients(p0, p1, p2, p3, tempCoeffs);
                if (i == 0)
                {
                    // Get first right.
                    vec3 pos0, pos0000001;
                    getPositionFromTAndCoefficients(tempCoeffs, 0.0f, pos0);
                    getPositionFromTAndCoefficients(tempCoeffs, 0.000001f, pos0000001);

                    vec3 delta;
                    glm_vec3_sub(pos0, pos0000001, delta);
                    glm_vec3_crossn(delta, vec3{ 0.0f, 1.0f, 0.0f }, rights[i].raw);
                }

                // Get current right.
                vec3 pos0999999, pos1;
                getPositionFromTAndCoefficients(tempCoeffs, 0.999999f, pos0999999);
                getPositionFromTAndCoefficients(tempCoeffs, 1.0f, pos1);

                vec3 delta;
                glm_vec3_sub(pos0999999, pos1, delta);
                glm_vec3_crossn(delta, vec3{ 0.0f, 1.0f, 0.0f }, rights[i + 1].raw);
            }
            if (calcType == 1)
            {
                // Calc coefficients.
                GondolaSystem_XData::FABCoefficients newFABCoefficients;

                for (auto& ts : _data->transSplines)
                    ts.transCoefficientsCache.resize(ts.cpIdxTo - ts.cpIdxFrom);

                float_t scales[2] = { -_data->lineSeparation, _data->lineSeparation };
                for (size_t si = 0; si < 2; si++)
                {
                    float_t scale = scales[si];
                    vec3 fp0, fp1, fp2, fp3;
                    vec3 fr0, fr1, fr2, fr3;
                    glm_vec3_scale(r0, scale, fr0);
                    glm_vec3_add(p0, fr0, fp0);
                    glm_vec3_scale(r1, scale, fr1);
                    glm_vec3_add(p1, fr1, fp1);
                    glm_vec3_scale(r2, scale, fr2);
                    glm_vec3_add(p2, fr2, fp2);
                    glm_vec3_scale(r3, scale, fr3);
                    glm_vec3_add(p3, fr3, fp3);
                    calculateSplineCoefficients(fp0, fp1, fp2, fp3, (si == 0 ? newFABCoefficients.leftSideCoefs : newFABCoefficients.rightSideCoefs));

                    // Calc transitional spline coefficients (if applicable).
                    for (auto& ts : _data->transSplines)
                    {
                        if (ts.cpIdxFrom <= i && ts.cpIdxTo - 1 >= i)
                        {
                            // Flip some control points to opposite side.
                            size_t diff = i - ts.cpIdxFrom;
                            if (diff >= 0)
                            {
                                glm_vec3_scale(r3, -scale, fr3);
                                glm_vec3_add(p3, fr3, fp3);
                            }
                            if (diff >= 1)
                            {
                                glm_vec3_scale(r2, -scale, fr2);
                                glm_vec3_add(p2, fr2, fp2);
                            }
                            if (diff >= 2)
                            {
                                glm_vec3_scale(r1, -scale, fr1);
                                glm_vec3_add(p1, fr1, fp1);
                            }
                            if (diff >= 3)  // @NOTE: this block would likely not be executed, but just in case.
                            {
                                glm_vec3_scale(r0, -scale, fr0);
                                glm_vec3_add(p0, fr0, fp0);
                            }
                            calculateSplineCoefficients(fp0, fp1, fp2, fp3, (si == 0 ? ts.transCoefficientsCache[diff].leftSideCoefs : ts.transCoefficientsCache[diff].rightSideCoefs));
                        }
                    }
                }
                _data->splineCoefficientsCache.push_back(newFABCoefficients);
            }
        }

        // @DEBUG: calculate the visualization for the spline and curve lines.
        _data->DEBUGCurveVisualization.splineLinePts.pts.clear();
        for (auto& cp : _data->controlPoints)
        {
            vec3s point;
            glm_vec3_copy(cp.position, point.raw);
            _data->DEBUGCurveVisualization.splineLinePts.pts.push_back(point);
        }
        
        // Get total distance between all control points.
        float_t cpTotalDist = 0.0f;
        for (size_t i = 1; i < _data->controlPoints.size(); i++)
            cpTotalDist += glm_vec3_distance(_data->controlPoints[i].position, _data->controlPoints[i - 1].position);

        // Step over curve.
        _data->DEBUGCurveVisualization.curveLinePts.clear();
        float_t stride = 1.0f / (cpTotalDist / _data->controlPoints.size()) * 2.0f;  // Take avg. distance and get the reciprocal to get the stride (mult by 2).
        for (uint8_t reverseRouteCounter = 0; reverseRouteCounter < 2; reverseRouteCounter++)
        {
            bool reverseRoute = (bool)reverseRouteCounter;
            for (uint8_t trackVersion = 0; trackVersion < _data->numTrackVersions; trackVersion++)
            {
                GondolaSystem_XData::DEBUGCurveVisualization::PointSet pointSet;
                for (float_t t = 0.0f; t < (float_t)_data->splineCoefficientsCache.size(); t += stride)
                {
                    vec3s point;
                    calculatePositionOnCurveFromT(_data, t, point.raw, reverseRoute, trackVersion);
                    pointSet.pts.push_back(point);
                }
                _data->DEBUGCurveVisualization.curveLinePts.push_back(pointSet);
            }
        }

        _data->triggerBakeSplineCache = false;
    }

    // Check whether player position is within any priority ranges.
    if (globalState::playerPositionRef == nullptr)
        return;  // No position reference was found. Exit.
    
    // Update gondola simulation collision.
    {
        float_t closestDistInRangeSqr = std::numeric_limits<float_t>::max();
        size_t  closestDistSimulationIdx = (size_t)-1;
        for (size_t i = 0; i < _data->simulations.size(); i++)
        {
            auto& simulation = _data->simulations[i];

            float_t currentDistance2 = glm_vec3_distance2(simulation.carts[0].calcCurrentROPos, *globalState::playerPositionRef);
            if (currentDistance2 < _data->detailedGondola.priorityRange * _data->detailedGondola.priorityRange &&
                currentDistance2 < closestDistInRangeSqr)
            {
                closestDistInRangeSqr = currentDistance2;
                closestDistSimulationIdx = i;
            }
        }
        if (closestDistSimulationIdx != (size_t)-1 &&
            _data->detailedGondola.prevClosestSimulation != closestDistSimulationIdx)
            readyGondolaInteraction(_data, _em, _data->simulations[closestDistSimulationIdx], closestDistSimulationIdx);
    }

    // Update station collision.
    {
        float_t closestDistInRangeSqr = std::numeric_limits<float_t>::max();
        size_t  closestDistStationIdx = (size_t)-1;
        for (size_t i = 0; i < _data->stations.size(); i++)
        {
            auto& station = _data->stations[i];

            float_t currentDistance2 = glm_vec3_distance2(_data->controlPoints[station.anchorCPIdx].position, *globalState::playerPositionRef);
            if (currentDistance2 < _data->detailedStation.priorityRange * _data->detailedStation.priorityRange &&
                currentDistance2 < closestDistInRangeSqr)
            {
                closestDistInRangeSqr = currentDistance2;
                closestDistStationIdx = i;
            }
        }
        if (closestDistStationIdx != (size_t)-1 &&
            _data->detailedStation.prevClosestStation != closestDistStationIdx)
            readyStationInteraction(_data, _data->stations[closestDistStationIdx], closestDistStationIdx);
    }
}

void GondolaSystem::update(const float_t& deltaTime)
{
}

void GondolaSystem::lateUpdate(const float_t& deltaTime)
{
    glm_mat4_identity(_data->controlRenderObj->transformMatrix);
    glm_translate(_data->controlRenderObj->transformMatrix, _data->position);

    for (auto& cp : _data->controlPoints)
    {
        glm_mat4_identity(cp.renderObj->transformMatrix);
        glm_translate(cp.renderObj->transformMatrix, cp.position);
    }

    for (auto& sim : _data->simulations)
        for (size_t i = 0; i < sim.renderObjs.size(); i++)
        {
            auto& ro = sim.renderObjs[i];
            glm_mat4_identity(ro->transformMatrix);
            glm_translate(ro->transformMatrix, sim.carts[i].calcCurrentROPos);
            glm_quat_rotate(ro->transformMatrix, sim.carts[i].calcCurrentRORot, ro->transformMatrix);
        }
    
    if (_data->detailedStation.prevClosestStation != (size_t)-1)
    {
        mat4 physicsInterpolTransform;
        _data->detailedStation.collision->getTransform(physicsInterpolTransform);
        vec3 offset;
        _data->detailedStation.collision->getSize(offset);
        glm_vec3_scale(offset, 0.5f, offset);
        glm_translate(physicsInterpolTransform, offset);
        mat4& renderObjTransform = _data->stations[_data->detailedStation.prevClosestStation].renderObj->transformMatrix;
        glm_mat4_copy(physicsInterpolTransform, renderObjTransform);
    }
}

void GondolaSystem::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpVec3(_data->position);

    float_t networkTypeF = (float_t)(int32_t)_data->gondolaNetworkType;
    ds.dumpFloat(networkTypeF);

    float_t numControlPtsF = (float_t)_data->controlPoints.size();
    ds.dumpFloat(numControlPtsF);
    for (auto& cp : _data->controlPoints)
        ds.dumpVec3(cp.position);

    float_t numStationsF = (float_t)_data->stations.size();
    ds.dumpFloat(numStationsF);
    for (auto& stn : _data->stations)
        ds.dumpFloat(stn.anchorCPIdx);
}

void GondolaSystem::load(DataSerialized& ds)
{
    Entity::load(ds);
    ds.loadVec3(_data->position);

    float_t networkTypeF;
    ds.loadFloat(networkTypeF);
    _data->gondolaNetworkType = GondolaSystem_XData::GondolaNetworkType((int32_t)networkTypeF);

    float_t numControlPtsF;
    ds.loadFloat(numControlPtsF);
    _data->controlPoints.resize((size_t)numControlPtsF, {});
    for (size_t i = 0; i < (size_t)numControlPtsF; i++)
        ds.loadVec3(_data->controlPoints[i].position);

    float_t numStationsF;
    ds.loadFloat(numStationsF);
    _data->stations.resize((size_t)numStationsF, {});
    for (size_t i = 0; i < (size_t)numStationsF; i++)
    {
        auto& station = _data->stations[i];
        float_t anchorCPIdxF;
        ds.loadFloat(anchorCPIdxF);
        station.anchorCPIdx = anchorCPIdxF;
    }  
}

bool GondolaSystem::processMessage(DataSerialized& message)
{
    return false;
}

size_t whichControlPointFromMatrix(GondolaSystem_XData* d, mat4* matrixToMove)
{
    std::lock_guard<std::mutex> lg(useControlPointsMutex);

    for (size_t i = 0; i < d->controlPoints.size(); i++)
    {
        auto& cp = d->controlPoints[i];
        if (matrixToMove == &cp.renderObj->transformMatrix)
            return i;
    }
    return (size_t)-1;
}

void GondolaSystem::reportMoved(mat4* matrixMoved)
{
    vec4 pos;
    mat4 rot;
    vec3 sca;
    glm_decompose(*matrixMoved, pos, rot, sca);

    // Whole system control point.
    if (matrixMoved == &_data->controlRenderObj->transformMatrix)
    {
        vec3 delta;
        glm_vec3_sub(pos, _data->position, delta);
        glm_vec3_copy(pos, _data->position);

        // Move all control points to new position.
        for (auto& cp : _data->controlPoints)
            glm_vec3_add(cp.position, delta, cp.position);
        _data->triggerBakeSplineCache = true;

        // Move all station render objs.
        mat4 translation = GLM_MAT4_IDENTITY_INIT;
        glm_translate(translation, delta);
        for (auto& station : _data->stations)
            glm_mat4_mul(translation, station.renderObj->transformMatrix, station.renderObj->transformMatrix);

        return;
    }

    // One of spline control points.
    size_t controlPointIdx = whichControlPointFromMatrix(_data, matrixMoved);
    if (controlPointIdx != (size_t)-1)
    {
        glm_vec3_copy(pos, _data->controlPoints[controlPointIdx].position);
        _data->triggerBakeSplineCache = true;

        return;
    }

    // One of stations.
    for (size_t i = 0; i < _data->stations.size(); i++)
    {
        auto& station = _data->stations[i];
        if (&station.renderObj->transformMatrix != matrixMoved)
            continue;

        updateControlPointPositions(_data, station, station.renderObj->transformMatrix);

        if (i == _data->detailedStation.prevClosestStation)
        {
            // Set transform for collision.
            mat4 matrixCopy;
            glm_mat4_copy(*matrixMoved, matrixCopy);
            vec3 extent;
            _data->detailedStation.collision->getSize(extent);
            glm_vec3_scale(extent, -0.5f, extent);
            glm_translate(matrixCopy, extent);
            vec4 collisionPos;
            mat4 collisionRot;
            vec3 collisionSca;
            glm_decompose(matrixCopy, collisionPos, collisionRot, collisionSca);
            versor rotationV;
            glm_mat4_quat(collisionRot, rotationV);
            _data->detailedStation.collision->moveBody(collisionPos, rotationV, true, 0.0f);
        }

        _data->triggerBakeSplineCache = true;
    }

    // Ignore movements to simulations.
}

void executeCAction(GondolaSystem_XData* d, GondolaSystem* _this, const std::string& myGuid, mat4* matrixToMove)
{
    size_t controlPointIdx = whichControlPointFromMatrix(d, matrixToMove);
    if (controlPointIdx == (size_t)-1)
        return;

    bool backwards = input::editorInputSet().backwardsModifier.holding;
    float_t directionMultiplier = 1.0f;
    if (backwards)
        directionMultiplier = -1.0f;

    std::lock_guard<std::mutex> lg(useControlPointsMutex);

    // Create new control point.
    GondolaSystem_XData::ControlPoint newControlPoint = {};
    vec3 otherPos;
    if (controlPointIdx == 0 && backwards)
    {
        vec3 delta;
        glm_vec3_sub(d->controlPoints[controlPointIdx].position, d->controlPoints[controlPointIdx + 1].position, delta);
        glm_vec3_scale(delta, 0.5f, delta);
        glm_vec3_add(d->controlPoints[controlPointIdx].position, delta, otherPos);
    }
    else if (controlPointIdx == d->controlPoints.size() -1 && !backwards)
    {
        vec3 delta;
        glm_vec3_sub(d->controlPoints[controlPointIdx].position, d->controlPoints[controlPointIdx - 1].position, delta);
        glm_vec3_scale(delta, 0.5f, delta);
        glm_vec3_add(d->controlPoints[controlPointIdx].position, delta, otherPos);
    }
    else
        glm_vec3_copy(d->controlPoints[controlPointIdx + (backwards ? -1 : 1)].position, otherPos);

    glm_vec3_add(
        d->controlPoints[controlPointIdx].position,
        otherPos,
        newControlPoint.position
    );
    glm_vec3_scale(newControlPoint.position, 0.5f, newControlPoint.position);

    d->rom->registerRenderObjects({
            {
                .model = d->rom->getModel("BuilderObj_BezierHandle", _this, [](){}),
                .renderLayer = RenderLayer::BUILDER,
                .attachedEntityGuid = myGuid,
            }
        },
        { &newControlPoint.renderObj }
    );

    d->controlPoints.insert(d->controlPoints.begin() + controlPointIdx + (backwards ? 0 : 1), newControlPoint);

    // Shift all indices of control points
    for (auto& station : d->stations)
        if (station.anchorCPIdx >= controlPointIdx + (backwards ? 0 : 1))
            station.anchorCPIdx++;
    calculateStationSecAuxCPIndices(d);

    d->triggerBakeSplineCache = true;
}

struct ReservedCP
{
    size_t cpIdx;
    size_t stationIdx;
};
std::vector<ReservedCP> getReservedControlPoints(GondolaSystem_XData* d)
{
    std::vector<ReservedCP> reservedCPs;
    for (int64_t i = d->stations.size() - 1; i >= 0; i--)  // Add in control points so that the station indices are descending so that deletions will not have to have an index recalculation.
    {
        auto& station = d->stations[i];
        reservedCPs.push_back({ station.anchorCPIdx, (size_t)i });
        reservedCPs.push_back({ station.secondaryForwardCPIdx, (size_t)i });
        reservedCPs.push_back({ station.secondaryBackwardCPIdx, (size_t)i });
        if (station.auxiliaryForwardCPIdx != (size_t)-1)
            reservedCPs.push_back({ station.auxiliaryForwardCPIdx, (size_t)i });
        if (station.auxiliaryBackwardCPIdx != (size_t)-1)
            reservedCPs.push_back({ station.auxiliaryBackwardCPIdx, (size_t)i });
    }
    return reservedCPs;
}

void executeXAction(GondolaSystem_XData* d, EntityManager* em, mat4* matrixToMove)
{
    size_t controlPointIdx = whichControlPointFromMatrix(d, matrixToMove);
    if (controlPointIdx == (size_t)-1)
        return;

    std::lock_guard<std::mutex> lg(useControlPointsMutex);

    // Delete any stations that use the control point that's getting deleted.
    std::vector<ReservedCP> reservedCPs = getReservedControlPoints(d);
    for (auto& rcp : reservedCPs)
        if (rcp.cpIdx == controlPointIdx)
        {
            // @COPYPASTA
            if (d->stations[rcp.stationIdx].renderObj != nullptr)
                d->rom->unregisterRenderObjects({ d->stations[rcp.stationIdx].renderObj });
            d->stations.erase(d->stations.begin() + rcp.stationIdx);
            if (d->detailedStation.collision != nullptr)
                destructAndResetStationCollision(d, em);
            break;  // Assume that none of the station control points are overlapping, so that means just the one needs to be deleted.
        }

    // Delete the control point.
    d->rom->unregisterRenderObjects({ d->controlPoints[controlPointIdx].renderObj });
    d->controlPoints.erase(d->controlPoints.begin() + controlPointIdx);
    
    // Shift all indices of control points
    for (auto& station : d->stations)
        if (station.anchorCPIdx > controlPointIdx)
            station.anchorCPIdx--;
    calculateStationSecAuxCPIndices(d);

    d->triggerBakeSplineCache = true;
}

void executeVAction(GondolaSystem_XData* d, EntityManager* em, GondolaSystem* _this, const std::string& myGuid, mat4* matrixToMove)
{
    size_t controlPointIdx = whichControlPointFromMatrix(d, matrixToMove);
    if (controlPointIdx == (size_t)-1)
        return;

    std::lock_guard<std::mutex> lg(useControlPointsMutex);

    if (controlPointIdx >= d->controlPoints.size() - 1)
        return;  // Too far to the end.
    if (controlPointIdx <= 0)
        return;  // Too far to the beginning.

    // Check to see if selected cp is already anchor of another station.
    // If so, then this action is taken as wanting to remove the station.
    for (size_t i = 0; i < d->stations.size(); i++)
        if (d->stations[i].anchorCPIdx == controlPointIdx)
        {
            // @COPYPASTA
            if (d->stations[i].renderObj != nullptr)
                d->rom->unregisterRenderObjects({ d->stations[i].renderObj });
            d->stations.erase(d->stations.begin() + i);
            if (d->detailedStation.collision != nullptr)
                destructAndResetStationCollision(d, em);
            return;  // Action done. Don't finish adding the new station in. Exit.
        }

    // Simply assign the control point idx for the anchor and
    // `calculateStationSecAuxCPIndices()` will take care of the rest.
    GondolaSystem_XData::Station newStation = {};
    newStation.anchorCPIdx = controlPointIdx;

    // Check if any of the control points are already taken. If so, overwrite other station.
    std::vector<ReservedCP> reservedCPs = getReservedControlPoints(d);
    std::vector<size_t> deletedStationIdxs;
    for (auto& reservedCP : reservedCPs)
    {
        if (std::find(deletedStationIdxs.begin(), deletedStationIdxs.end(), reservedCP.stationIdx) != deletedStationIdxs.end())
            continue;

        if (newStation.anchorCPIdx == reservedCP.cpIdx ||
            newStation.secondaryForwardCPIdx == reservedCP.cpIdx ||
            newStation.secondaryBackwardCPIdx == reservedCP.cpIdx ||
            newStation.auxiliaryForwardCPIdx == reservedCP.cpIdx ||
            newStation.auxiliaryBackwardCPIdx == reservedCP.cpIdx)
        {
            // @COPYPASTA
            if (d->stations[reservedCP.stationIdx].renderObj != nullptr)
                d->rom->unregisterRenderObjects({ d->stations[reservedCP.stationIdx].renderObj });
            d->stations.erase(d->stations.begin() + reservedCP.stationIdx);
            if (d->detailedStation.collision != nullptr)
                destructAndResetStationCollision(d, em);
            deletedStationIdxs.push_back(reservedCP.stationIdx);
        }
    }

    d->stations.push_back(newStation);
    calculateStationSecAuxCPIndices(d);
    loadAllMissingStationRenderObjs(d, _this, myGuid);

    d->triggerBakeSplineCache = true;
}

void GondolaSystem::renderImGui()
{
    // Process keyboard actions.
    ImGui::Text(
        "C: add a control point ahead (hold shift for behind).\n"
        "X: delete selected control point.\n"
        "V: assign/unassign station at control point."
    );
    if (input::editorInputSet().actionC.onAction)
        executeCAction(_data, this, getGUID(), _engine->getMatrixToMove());
    if (input::editorInputSet().actionX.onAction)
        executeXAction(_data, _em, _engine->getMatrixToMove());
    if (input::editorInputSet().actionV.onAction)
        executeVAction(_data, _em, this, getGUID(), _engine->getMatrixToMove());

    // Imgui.
    ImGui::Checkbox("Show Curve Paths", &showCurvePaths);
    if (ImGui::Button("Spawn Simulation"))
        spawnSimulation(_data, this, getGUID(), -_data->gondolaSimulationGlobalTimer);
}
