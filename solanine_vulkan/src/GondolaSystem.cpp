#include "GondolaSystem.h"

#include <Jolt/Jolt.h>
#include "EntityManager.h"
#include "RenderObject.h"
#include "PhysicsEngine.h"
#include "HotswapResources.h"
#include "SceneManagement.h"
#include "GlobalState.h"
#include "VoxelField.h"
#include "imgui/imgui.h"


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
    bool                             triggerBakeSplineCache = true;  // Do bake right at initialization.

    enum class GondolaNetworkType
    {
        NONE = 0,
        FUTSUU,
        JUNKYUU,
        KAISOKU,
        TOKKYUU,
    };
    //GondolaNetworkType gondolaNetworkType = GondolaNetworkType::NONE;
    GondolaNetworkType gondolaNetworkType = GondolaNetworkType::FUTSUU;  // @NOCHECKIN

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
        float_t                    positionT;
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
        float_t priorityRange = 20000000.0f;
        size_t prevClosestSimulation = (size_t)-1;
        std::vector<VoxelField*> collisions;  // Collision objects for the most nearby train to the player character.
    } detailedGondola;
};

void buildCollisions(GondolaSystem_XData* d, VulkanEngine* engineRef, std::vector<VoxelField*>& outCollisions, GondolaSystem_XData::GondolaNetworkType networkType)
{
    // Load prefab from file.
    std::vector<Entity*> ents;
    switch (networkType)
    {
        case GondolaSystem_XData::GondolaNetworkType::NONE:
            std::cerr << "[BUILD COLLISIONS]" << std::endl
                << "WARNING: Gondola network type was set to NONE, so no collision object prefab was spawned." << std::endl;
            return;
            
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

void destructAndResetCollisions(GondolaSystem_XData* d, EntityManager* em)
{
    for (auto& collision : d->detailedGondola.collisions)
        em->destroyOwnedEntity((Entity*)collision);
    d->detailedGondola.collisions.clear();
}

void readyGondolaInteraction(GondolaSystem_XData* d, EntityManager* em, const GondolaSystem_XData::Simulation& simulation)
{
    // Clear and rebuild
    destructAndResetCollisions(d, em);
    buildCollisions(d, d->engineRef, d->detailedGondola.collisions, d->gondolaNetworkType);
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
        std::vector<RenderObject> inROs;
        inROs.resize(4, {
            .model = _data->rom->getModel("BuilderObj_BezierHandle", this, [](){}),
            .renderLayer = RenderLayer::BUILDER,
            .attachedEntityGuid = getGUID(),
        });
        std::vector<RenderObject**> outROs;
        _data->controlPoints.resize(4, {});
        for (size_t i = 0; i < _data->controlPoints.size(); i++)
        {
            auto& cp = _data->controlPoints[i];
            glm_vec3_add(_data->position, vec3{ 0.0f, -5.0f, (float_t)i }, cp.position);
            outROs.push_back(&cp.renderObj);
        }
        _data->rom->registerRenderObjects(inROs, outROs);
    }
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

    destructAndResetCollisions(_data, _em);

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

void spawnSimulation(GondolaSystem_XData* d, void* modelOwner, const std::string& guid, float_t spawnT)
{
    // Register cart render objects.
    constexpr size_t NUM_CARTS_LOCAL_NETWORK = 4;
    constexpr float_t LENGTH_LOCAL_NETWORK = 26.0f;
    constexpr float_t MARGIN_LOCAL_NETWORK = 1.0f;

    GondolaSystem_XData::Simulation newSimulation;
    newSimulation.positionT = spawnT;
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
        float_t frontMargin = (i == 0 ? 0.0f : MARGIN_LOCAL_NETWORK);
        float_t rearMargin = (i == NUM_CARTS_LOCAL_NETWORK - 1 ? 0.0f : MARGIN_LOCAL_NETWORK);
        GondolaSystem_XData::Simulation::GondolaCart newCart = {
            .length = LENGTH_LOCAL_NETWORK,
            .frontMargin = frontMargin,
            .rearMargin = rearMargin,
            .bogiePadding = LENGTH_LOCAL_NETWORK / 6.5f,  // This is the measured proportion on the Japanese trains on Yamanote-sen.
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

void updateSimulation(GondolaSystem_XData* d, EntityManager* em, size_t simIdx, const float_t& physicsDeltaTime)
{
    GondolaSystem_XData::Simulation& ioSimulation = d->simulations[simIdx];
    ioSimulation.positionT += physicsDeltaTime;

    // Position each render object position based off the position of the bogies.
    float_t currentPosT = ioSimulation.positionT;
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
                destructAndResetCollisions(d, em);
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

        // Update physics objects.
        if (d->detailedGondola.prevClosestSimulation == simIdx)
        {
            vec3 extent;
            d->detailedGondola.collisions[i]->getSize(extent);
            glm_vec3_scale(extent, -0.5f, extent);
            glm_mat4_mulv3(rotation, extent, 0.0f, extent);
            vec3 kinematicPos;
            glm_vec3_add(cart.calcCurrentROPos, extent, kinematicPos);
            d->detailedGondola.collisions[i]->moveBodyKinematic(kinematicPos, cart.calcCurrentRORot, physicsDeltaTime);
        }
    }
}

void GondolaSystem::physicsUpdate(const float_t& physicsDeltaTime)
{
    drawDEBUGCurveVisualization(_data);

    // Update simulations.
    // @TODO: there should be some kind of timeslicing for this, then update the timestamp of the new calculated point, and in `lateUpdate()` interpolate between the two generated points.
    // @REPLY: and then for the closest iterating one, there should be `updateSimulation` running every frame, since this affects the collisions too.
    //         In order to accomplish this, having a global timer is important. Then that way each timesliced gondola won't have to guess the timing and then it gets off.
    //         Just pass in the global timer value instead of `physicsDeltaTime`.  @TODO
    for (int64_t i = _data->simulations.size() - 1; i >= 0; i--)  // Reverse iteration so that delete can happen.
        updateSimulation(_data, _em, i, physicsDeltaTime);

    if (!_data->timeslicing.checkTimeslice())
        return;  // Exit bc not timesliced position.

    // Rebake curve visualization.
    if (_data->triggerBakeSplineCache)
    {
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

    readyGondolaInteraction(_data, _em, _data->simulations[closestDistSimulationIdx]);
    _data->detailedGondola.prevClosestSimulation = closestDistSimulationIdx;  // Mark cache as completed.
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
}

void GondolaSystem::dump(DataSerializer& ds)
{
    Entity::dump(ds);
}

void GondolaSystem::load(DataSerialized& ds)
{
    Entity::load(ds);
}

bool GondolaSystem::processMessage(DataSerialized& message)
{
    return false;
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
    for (auto& cp : _data->controlPoints)
    {
        if (matrixMoved == &cp.renderObj->transformMatrix)
        {
            glm_vec3_copy(pos, cp.position);
            _data->triggerBakeSplineCache = true;
            return;
        }
    }

    // Ignore movements to simulations.
}

void GondolaSystem::renderImGui()
{
    if (ImGui::Button("Spawn Simulation"))
        spawnSimulation(_data, this, getGUID(), 0.0f);
}
