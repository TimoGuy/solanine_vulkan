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

    struct BSplineCoefficients
    {
        vec4 coefficientsX;
        vec4 coefficientsY;
        vec4 coefficientsZ;
    };
    std::vector<BSplineCoefficients> splineCoefficientsCache;
    struct DEBUGCurveVisualization
    {
        std::vector<vec3s> splineLinePts;
        std::vector<vec3s> curveLinePts;
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

    struct DetailedGondola
    {
        bool active = false;
        float_t priorityRange = 500.0f;  // This should be enough to support up to 16 cart long gondolas.
        size_t prevClosestSimulation = (size_t)-1;
        std::vector<VoxelField*> collisions;  // Collision objects for the most nearby train to the player character.
    } detailedGondola;

    struct Station
    {
        struct AssignedControlPoint
        {
            size_t cpIdx = (size_t)-1;
            vec3   localOffset = GLM_VEC3_ZERO_INIT;
        };
        AssignedControlPoint anchorCP;
        AssignedControlPoint secondaryForwardCP;
        AssignedControlPoint secondaryBackwardCP;
        AssignedControlPoint auxiliaryForwardCP;
        AssignedControlPoint auxiliaryBackwardCP;
        RenderObject* renderObj = nullptr;
    };
    std::vector<Station> stations;

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
        float_t tTarget = (float_t)d->stations[i].anchorCP.cpIdx + tTargetStationArrivalOffset * (reverseRoute ? -1.0f : 1.0f);
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

    outT = tDistFromEndOfStationToBeginningOfGondola + tLengthOfGondolaUpToLeadingBogie;  // Start where the first bogie would be placed.
    if (time >= totalSimLength * 0.5f)
    {
        outT = (float_t)d->controlPoints.size() - 0.000001f - outT;
        reverseRoute = true;
        time -= totalSimLength * 0.5f;
    }
    else
        reverseRoute = false;
    
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
                    // No new station is found.
                    std::cerr << "HEYO THERES NO MORE STATION!!!!";
                }

                float_t maxAbleToMove = (nextT - outT) - ENTER_LEAVE_STATION_TIME;
                if (time < maxAbleToMove)
                {
                    // Move for the whole time.
                    outT += time;
                    time = 0.0f;
                }
                else
                {
                    // Move just the max, then switch to `ARRIVE_INTO_STATION`.
                    outT += maxAbleToMove;
                    time -= maxAbleToMove;
                    currentSimulationStage = SimulationStage::ARRIVE_INTO_STATION;
                }
            } break;

            case SimulationStage::LEAVE_STATION:
            {
                if (time >= ENTER_LEAVE_STATION_TIME)
                    outT += 1.0f;
                else
                    outT += std::powf(time / ENTER_LEAVE_STATION_TIME, ENTER_LEAVE_STATION_TIME);
                time -= ENTER_LEAVE_STATION_TIME;
                currentSimulationStage = SimulationStage::TRAVEL;
            } break;

            case SimulationStage::ARRIVE_INTO_STATION:
            {
                if (time >= ENTER_LEAVE_STATION_TIME)
                    outT += 1.0f;
                else
                    outT += std::powf((time - ENTER_LEAVE_STATION_TIME) / ENTER_LEAVE_STATION_TIME + 1.0f, ENTER_LEAVE_STATION_TIME);  // https://www.desmos.com/calculator/8r16nz38wb
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
}

GondolaSystem::~GondolaSystem()
{
    hotswapres::removeOwnedCallbacks(this);

    std::vector<RenderObject*> renderObjsToUnregister;
    renderObjsToUnregister.push_back(_data->controlRenderObj);
    for (auto& sim : _data->simulations)
        for (auto& ro : sim.renderObjs)
            renderObjsToUnregister.push_back(ro);
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

bool calculatePositionOnCurveFromT(GondolaSystem_XData* d, float_t t, vec3& outPosition)
{
    float_t wholeT, remainderT;
    remainderT = std::modf(t, &wholeT);

    if (t < 0.0f ||
        (size_t)wholeT >= d->splineCoefficientsCache.size())
        return false;  // Return false bc out of range.

    auto& coefficients = d->splineCoefficientsCache[(size_t)wholeT];

    vec4 tInputs = {
        1.0f,
        remainderT,
        remainderT * remainderT,
        remainderT * remainderT * remainderT
    };

    outPosition[0] = glm_vec4_dot(tInputs, coefficients.coefficientsX);
    outPosition[1] = glm_vec4_dot(tInputs, coefficients.coefficientsY);
    outPosition[2] = glm_vec4_dot(tInputs, coefficients.coefficientsZ);
    return true;
}

void drawDEBUGCurveVisualization(GondolaSystem_XData* d)
{
    for (size_t i = 1; i < d->DEBUGCurveVisualization.splineLinePts.size(); i++)
        physengine::drawDebugVisLine(
            d->DEBUGCurveVisualization.splineLinePts[i - 1].raw,
            d->DEBUGCurveVisualization.splineLinePts[i].raw,
            physengine::DebugVisLineType::KIKKOARMY
        );
    for (size_t i = 1; i < d->DEBUGCurveVisualization.curveLinePts.size(); i++)
        physengine::drawDebugVisLine(
            d->DEBUGCurveVisualization.curveLinePts[i - 1].raw,
            d->DEBUGCurveVisualization.curveLinePts[i].raw,
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

bool searchForRightTOnCurve(GondolaSystem_XData* d, float_t& ioT, vec3 anchorPos, float_t targetDistance, float_t startingSearchDirection)
{
    float_t targetDistance2 = targetDistance * targetDistance;

    float_t searchStride = 0.5f;
    float_t searchDirection = startingSearchDirection;
    float_t searchPosDistWS2 = std::numeric_limits<float_t>::max();
    float_t maxT = (float_t)d->splineCoefficientsCache.size() - 0.000001f;

    while (std::abs(targetDistance2 - searchPosDistWS2) > 0.1f)  // This should be around 8 tries... maybe.
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
        calculatePositionOnCurveFromT(d, ioT, searchPosition);

        searchPosDistWS2 = glm_vec3_distance2(anchorPos, searchPosition);

        if (searchPosDistWS2 > targetDistance2)
        {
            if (searchDirection < 0.0f)
                searchStride *= 0.5f;  // Cut stride in half since crossed the target pos.
            else if (maybeWantingToGoFurtherIntoUndefined)
                return false;  // Doesn't want to turn around after dipping into undefined zone. Exit.
            searchDirection = 1.0f;
        }
        else
        {
            if (searchDirection > 0.0f)
                searchStride *= 0.5f;  // Cut stride in half since crossed the target pos.
            else if (maybeWantingToGoFurtherIntoUndefined)
                return false;  // Doesn't want to turn around after dipping into undefined zone. Exit.
            searchDirection = -1.0f;
        }
    }

    return true;
}

void updateSimulation(GondolaSystem_XData* d, EntityManager* em, size_t simIdx, const float& physicsDeltaTime)
{
    GondolaSystem_XData::Simulation& ioSimulation = d->simulations[simIdx];

    // Position each render object position based off the position of the bogies.
    float_t currentPosT;
    bool reverseRoute;
    getTFromSimulationTime(d, d->gondolaSimulationGlobalTimer + ioSimulation.offsetT, currentPosT, reverseRoute);
    for (size_t i = 0; i < ioSimulation.carts.size(); i++)
    {
        auto& cart = ioSimulation.carts[i];

        // Move to first bogie position.
        if (i > 0)
        {
            auto& prevCart = ioSimulation.carts[i - 1];
            float_t distanceToNext = prevCart.bogiePadding + prevCart.rearMargin + cart.frontMargin + cart.bogiePadding;
            searchForRightTOnCurve(d, currentPosT, prevCart.bogiePosition2, distanceToNext, -1.0f);
        }

        if (!calculatePositionOnCurveFromT(d, currentPosT, cart.bogiePosition1))
        {
            // Remove simulation if out of range.
            // @NOTE: @INCOMPLETE: this shouldn't happen. At the beginning there should be X number of gondolas spawned and then they all go in a uniform loop.
            if (d->detailedGondola.prevClosestSimulation == simIdx)
                destructAndResetGondolaCollisions(d, em);
            d->rom->unregisterRenderObjects(ioSimulation.renderObjs);
            d->simulations.erase(d->simulations.begin() + simIdx);
            d->detailedGondola.prevClosestSimulation = (size_t)-1;  // Invalidate detailedGondola collision cache.
        }

        // Move to second bogie position.
        float_t distanceToSecond = cart.length - 2.0f * cart.bogiePadding;
        searchForRightTOnCurve(d, currentPosT, cart.bogiePosition1, distanceToSecond, -1.0f);

        calculatePositionOnCurveFromT(d, currentPosT, cart.bogiePosition2);

        // Create new transform.
        glm_vec3_copy(cart.calcCurrentROPos, cart.calcPrevROPos);
        glm_quat_copy(cart.calcCurrentRORot, cart.calcPrevRORot);

        glm_vec3_add(cart.bogiePosition1, cart.bogiePosition2, cart.calcCurrentROPos);
        glm_vec3_scale(cart.calcCurrentROPos, 0.5f, cart.calcCurrentROPos);

        vec3 delta;
        glm_vec3_sub(cart.bogiePosition1, cart.bogiePosition2, delta);
        float_t yRot = std::atan2f(delta[0], delta[2]) + M_PI;
        
        float_t xzDist = glm_vec2_norm(vec2{ delta[0], delta[2] });
        float_t xRot = std::atan2f(delta[1], xzDist);

        mat4 rotation;
        glm_euler_zyx(vec3{ xRot, yRot, 0.0f }, rotation);
        glm_mat4_quat(rotation, cart.calcCurrentRORot);

    }

    // Update physics objects.
    moveCollisionBodies(d, false, physicsDeltaTime);
}

void updateStation(GondolaSystem_XData* d)
{
    auto& station = d->stations[d->detailedStation.prevClosestStation];
    mat4 transform;
    d->detailedStation.collision->getTransform(transform);

    // if (glm_mat4_similar(d->detailedStation.prevCollisionTransform, transform))  // @INCOMPLETE: there's no way to tell if the new transform is similar to the old one.
    //     return;

    vec3 extent;
    d->detailedStation.collision->getSize(extent);
    glm_vec3_scale(extent, 0.5f, extent);
    glm_translate(transform, extent);

    glm_mat4_mulv3(transform, station.anchorCP.localOffset, 1.0f, d->controlPoints[station.anchorCP.cpIdx].position);
    glm_mat4_mulv3(transform, station.secondaryForwardCP.localOffset, 1.0f, d->controlPoints[station.secondaryForwardCP.cpIdx].position);
    glm_mat4_mulv3(transform, station.secondaryBackwardCP.localOffset, 1.0f, d->controlPoints[station.secondaryBackwardCP.cpIdx].position);
    if (station.auxiliaryForwardCP.cpIdx != (size_t)-1)
        glm_mat4_mulv3(transform, station.auxiliaryForwardCP.localOffset, 1.0f, d->controlPoints[station.auxiliaryForwardCP.cpIdx].position);
    if (station.auxiliaryBackwardCP.cpIdx != (size_t)-1)
        glm_mat4_mulv3(transform, station.auxiliaryBackwardCP.localOffset, 1.0f, d->controlPoints[station.auxiliaryBackwardCP.cpIdx].position);
    
    d->triggerBakeSplineCache = true;  // Assumption is that the station moved.
}

void GondolaSystem::physicsUpdate(const float_t& physicsDeltaTime)
{
    drawDEBUGCurveVisualization(_data);

    // Update simulations.
    // @TODO: there should be some kind of timeslicing for this, then update the timestamp of the new calculated point, and in `lateUpdate()` interpolate between the two generated points.
    // @REPLY: and then for the closest iterating one, there should be `updateSimulation` running every frame, since this affects the collisions too.
    //         In order to accomplish this, having a global timer is important. Then that way each timesliced gondola won't have to guess the timing and then it gets off.
    //         Just pass in the global timer value instead of `physicsDeltaTime`.  @TODO
    _data->gondolaSimulationGlobalTimer += physicsDeltaTime;
    for (int64_t i = _data->simulations.size() - 1; i >= 0; i--)  // Reverse iteration so that delete can happen.
        updateSimulation(_data, _em, i, physicsDeltaTime);

    if (_data->detailedStation.collision != nullptr)
        updateStation(_data);

    if (!_data->timeslicing.checkTimeslice())
        return;  // Exit bc not timesliced position.

    // Rebake curve visualization.
    if (_data->triggerBakeSplineCache)
    {
        std::lock_guard<std::mutex> lg(useControlPointsMutex);

        // Calculate coefficient cache.
        _data->splineCoefficientsCache.clear();
        for (size_t i = 0; i < _data->controlPoints.size() - 1; i++)
        {
            vec3 p0, p1, p2, p3;
            glm_vec3_copy(_data->controlPoints[i].position, p1);
            glm_vec3_copy(_data->controlPoints[i + 1].position, p2);

            if (i == 0)
            {
                // First point (mirror 2nd control point over 1st control point to get -1st control point).
                vec3 inter;
                glm_vec3_sub(p1, p2, inter);
                glm_vec3_add(inter, p1, p0);
            }
            else
                glm_vec3_copy(_data->controlPoints[i - 1].position, p0);

            if (i == _data->controlPoints.size() - 2)
            {
                // Last point (mirror 2nd to last control point over last control point to get ghost point).
                vec3 inter;
                glm_vec3_sub(p2, p1, inter);
                glm_vec3_add(inter, p2, p3);
            }
            else
                glm_vec3_copy(_data->controlPoints[i + 2].position, p3);

            GondolaSystem_XData::BSplineCoefficients newBSplineCoefficient;
            calculateSplineCoefficients(p0, p1, p2, p3, newBSplineCoefficient);
            _data->splineCoefficientsCache.push_back(newBSplineCoefficient);
        }

        // @DEBUG: calculate the visualization for the spline and curve lines.
        _data->DEBUGCurveVisualization.splineLinePts.clear();
        for (auto& cp : _data->controlPoints)
        {
            vec3s point;
            glm_vec3_copy(cp.position, point.raw);
            _data->DEBUGCurveVisualization.splineLinePts.push_back(point);
        }
        
        // Get total distance between all control points.
        float_t cpTotalDist = 0.0f;
        for (size_t i = 1; i < _data->controlPoints.size(); i++)
            cpTotalDist += glm_vec3_distance(_data->controlPoints[i].position, _data->controlPoints[i - 1].position);

        // Step over curve.
        _data->DEBUGCurveVisualization.curveLinePts.clear();
        float_t stride = 1.0f / (cpTotalDist / _data->controlPoints.size());  // Take avg. distance and get the reciprocal to get the stride.
        for (float_t t = 0.0f; t < (float_t)_data->splineCoefficientsCache.size(); t += stride)
        {
            vec3s point;
            calculatePositionOnCurveFromT(_data, t, point.raw);
            _data->DEBUGCurveVisualization.curveLinePts.push_back(point);
        }

        _data->triggerBakeSplineCache = false;
    }

    // Check whether player position is within any priority ranges.
    if (globalState::playerPositionRef == nullptr)
        return;  // No position reference was found. Exit.
    
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
    if (closestDistSimulationIdx == (size_t)-1)
        return;  // No position in range was found. Exit.

    if (_data->detailedGondola.prevClosestSimulation == closestDistSimulationIdx)
        return;  // Already created. No need to recreate. Exit.

    readyGondolaInteraction(_data, _em, _data->simulations[closestDistSimulationIdx], closestDistSimulationIdx);
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

        return;
    }

    // Check to see if spline control point.
    size_t controlPointIdx = whichControlPointFromMatrix(_data, matrixMoved);
    if (controlPointIdx != (size_t)-1)
    {
        glm_vec3_copy(pos, _data->controlPoints[controlPointIdx].position);
        _data->triggerBakeSplineCache = true;
    }

    // Ignore movements to simulations.
}

void executeCAction(GondolaSystem_XData* d, GondolaSystem* _this, const std::string& myGuid, mat4* matrixToMove)
{
    size_t controlPointIdx = whichControlPointFromMatrix(d, matrixToMove);
    if (controlPointIdx == (size_t)-1)
        return;

    bool backwards = input::keyShiftPressed;
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
    d->triggerBakeSplineCache = true;
}

void executeXAction(GondolaSystem_XData* d, mat4* matrixToMove)
{
    size_t controlPointIdx = whichControlPointFromMatrix(d, matrixToMove);
    if (controlPointIdx == (size_t)-1)
        return;

    std::lock_guard<std::mutex> lg(useControlPointsMutex);

    d->rom->unregisterRenderObjects({ d->controlPoints[controlPointIdx].renderObj });

    d->controlPoints.erase(d->controlPoints.begin() + controlPointIdx);
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

    // Check to see if control point has enough room
    // (need 5 if middle, 4 if on one of the ends, bc ghost point will act as the 5th point).
    GondolaSystem_XData::Station newStation = {};
    newStation.anchorCP.cpIdx = controlPointIdx;
    newStation.secondaryForwardCP.cpIdx = controlPointIdx + 1;
    newStation.secondaryBackwardCP.cpIdx = controlPointIdx - 1;
    if (controlPointIdx < d->controlPoints.size() - 2)
        newStation.auxiliaryForwardCP.cpIdx = controlPointIdx + 2;
    if (controlPointIdx > 1)
        newStation.auxiliaryBackwardCP.cpIdx = controlPointIdx - 2;

    // Check if any of the control points are already taken. If so, overwrite other station.
    struct ReservedCP
    {
        size_t cpIdx;
        size_t stationIdx;
    };
    std::vector<ReservedCP> reservedCPs;
    for (int64_t i = d->stations.size() - 1; i >= 0; i--)
    {
        auto& station = d->stations[i];
        if (d->stations[i].anchorCP.cpIdx == newStation.anchorCP.cpIdx)
        {
            // Found self. That means wants to remove the station from here.
            // @COPYPASTA
            if (d->stations[i].renderObj != nullptr)
                d->rom->unregisterRenderObjects({ d->stations[i].renderObj });
            d->stations.erase(d->stations.begin() + i);
            if (d->detailedStation.collision != nullptr)
                destructAndResetStationCollision(d, em);
            return;  // Action done. Don't finish adding the new station in. Exit.
        }
        reservedCPs.push_back({ station.anchorCP.cpIdx, (size_t)i });
        reservedCPs.push_back({ station.secondaryForwardCP.cpIdx, (size_t)i });
        reservedCPs.push_back({ station.secondaryBackwardCP.cpIdx, (size_t)i });
        if (station.auxiliaryForwardCP.cpIdx != (size_t)-1)
            reservedCPs.push_back({ station.auxiliaryForwardCP.cpIdx, (size_t)i });
        if (station.auxiliaryBackwardCP.cpIdx != (size_t)-1)
            reservedCPs.push_back({ station.auxiliaryBackwardCP.cpIdx, (size_t)i });
    }
    std::vector<size_t> deletedStationIdxs;
    for (auto& reservedCP : reservedCPs)
    {
        if (std::find(deletedStationIdxs.begin(), deletedStationIdxs.end(), reservedCP.stationIdx) != deletedStationIdxs.end())
            continue;

        if (newStation.anchorCP.cpIdx == reservedCP.cpIdx ||
            newStation.secondaryForwardCP.cpIdx == reservedCP.cpIdx ||
            newStation.secondaryBackwardCP.cpIdx == reservedCP.cpIdx ||
            newStation.auxiliaryForwardCP.cpIdx == reservedCP.cpIdx ||
            newStation.auxiliaryBackwardCP.cpIdx == reservedCP.cpIdx)
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

    // Line up the control points to the anchor point.
    vec3 localOffsetDelta = { 0.0f, 0.0f, LENGTH_STATION_LOCAL_NETWORK * 0.5f };
    glm_vec3_add(
        newStation.anchorCP.localOffset,
        localOffsetDelta,
        newStation.secondaryForwardCP.localOffset
    );
    glm_vec3_sub(
        newStation.anchorCP.localOffset,
        localOffsetDelta,
        newStation.secondaryBackwardCP.localOffset
    );

    // Line up the aux points too.
    if (newStation.auxiliaryForwardCP.cpIdx != (size_t)-1)
        glm_vec3_add(
            newStation.secondaryForwardCP.localOffset,
            localOffsetDelta,
            newStation.auxiliaryForwardCP.localOffset
        );
    if (newStation.auxiliaryBackwardCP.cpIdx != (size_t)-1)
        glm_vec3_sub(
            newStation.secondaryBackwardCP.localOffset,
            localOffsetDelta,
            newStation.auxiliaryBackwardCP.localOffset
        );

    d->triggerBakeSplineCache = true;

    d->stations.push_back(newStation);

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
        d->controlPoints[newStation.secondaryForwardCP.cpIdx].position,
        d->controlPoints[newStation.secondaryBackwardCP.cpIdx].position,
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
    glm_vec3_add(d->controlPoints[newStation.anchorCP.cpIdx].position, extent, newPos);

    d->detailedStation.collision->moveBody(newPos, rotationV, true, 0.0f);
    d->detailedStation.prevClosestStation = d->stations.size() - 1;  // Get most recent pushed back station.
    // glm_mat4_zero(d->detailedStation.prevCollisionTransform);  // Invalidate prev collision cache.  // @INCOMPLETE: there's no way to tell if the new transform is similar to the old one.

    d->rom->registerRenderObjects({
            {
                .model = d->rom->getModel("BuilderObj_GondolaStation", _this, [](){}),
                .renderLayer = RenderLayer::BUILDER,
                .attachedEntityGuid = myGuid,
            }
        },
        { &d->stations.back().renderObj }
    );

    // Set initial transform for station renderObj.
    auto& station = d->stations.back();
    glm_mat4_identity(station.renderObj->transformMatrix);
    glm_translate(station.renderObj->transformMatrix, newPos);
    glm_quat_rotate(station.renderObj->transformMatrix, rotationV, station.renderObj->transformMatrix);
}

void GondolaSystem::renderImGui()
{
    // Process keyboard actions.
    ImGui::Text("C: add a control point ahead (hold shift for behind).\nX: delete selected control point.\nV: assign/unassign station at control point.");
    static bool prevCHeld = false;
    static bool prevXHeld = false;
    static bool prevVHeld = false;
    if (input::keyCPressed && !prevCHeld)
        executeCAction(_data, this, getGUID(), _engine->getMatrixToMove());
    prevCHeld = input::keyCPressed;
    if (input::keyXPressed && !prevXHeld)
        executeXAction(_data, _engine->getMatrixToMove());
    prevXHeld = input::keyXPressed;
    if (input::keyVPressed && !prevVHeld)
        executeVAction(_data, _em, this, getGUID(), _engine->getMatrixToMove());
    prevVHeld = input::keyVPressed;

    // Imgui.
    if (ImGui::Button("Spawn Simulation"))
        spawnSimulation(_data, this, getGUID(), -_data->gondolaSimulationGlobalTimer);
}
