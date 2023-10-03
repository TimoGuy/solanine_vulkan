#include "GondolaSystem.h"

#include <Jolt/Jolt.h>
#include "EntityManager.h"
#include "RenderObject.h"
#include "PhysicsEngine.h"
#include "HotswapResources.h"
#include "SceneManagement.h"
#include "GlobalState.h"
#include "VoxelField.h"


struct GondolaSystem_XData
{
    VulkanEngine*              engineRef;
    RenderObjectManager*       rom;
    RenderObject*              controlRenderObj;
    std::vector<VoxelField*>   collisions;  // Collision objects for the most nearby train to the player character.

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
    GondolaNetworkType gondolaNetworkType = GondolaNetworkType::NONE;
    bool               gondolaCollisionActive = false;
    bool               playerCharWithinRange = false;
    float_t            priorityRange = 200.0f;

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
        float_t            positionT;
        RenderObject*      renderObj;
        vec3               calculatedPositionWorld;
    };
    std::vector<Simulation> simulations;
    size_t simSpawnTimer = 0;
};

void buildCollisions(GondolaSystem_XData* d, VulkanEngine* engineRef, std::vector<VoxelField*>& outCollisions, GondolaSystem_XData::GondolaNetworkType networkType)
{
    // Load prefab from file.
    std::vector<Entity*> ents;
    switch (networkType)
    {
        case GondolaSystem_XData::GondolaNetworkType::NONE:
            return;
            
        case GondolaSystem_XData::GondolaNetworkType::FUTSUU:
            scene::loadPrefab("res/prefabs/gondola_collision_futsuu.hunk", engineRef, ents);  // Supposed to contain all the cars for collision (even though there are repeats in the collision data).
            break;

        case GondolaSystem_XData::GondolaNetworkType::JUNKYUU:
            scene::loadPrefab("res/prefabs/gondola_collision_junkyuu.hunk", engineRef, ents);
            break;

        case GondolaSystem_XData::GondolaNetworkType::KAISOKU:
            scene::loadPrefab("res/prefabs/gondola_collision_kaisoku.hunk", engineRef, ents);
            break;

        case GondolaSystem_XData::GondolaNetworkType::TOKKYUU:
            scene::loadPrefab("res/prefabs/gondola_collision_tokkyuu.hunk", engineRef, ents);
            break;
    }

    // Cast prefab contents into VoxelFields.
    for (auto& ent : ents)
    {
        VoxelField* entAsVF;
        if (entAsVF = dynamic_cast<VoxelField*>(ent))
            outCollisions.push_back(entAsVF);
    }
}

void destructAndResetCollisions(GondolaSystem_XData* d, EntityManager* em)
{
    for (auto& collision : d->collisions)
        em->destroyOwnedEntity((Entity*)collision);
    d->collisions.clear();
}

void readyGondolaInteraction(GondolaSystem_XData* d, EntityManager* em, const GondolaSystem_XData::Simulation& simulation)
{
    // @TODO: This is how I want this to work. Have a timer that runs if player is out of range. if the time runs past 5 seconds, unload collision. As soon as player gets into range, load in collision for the nearest simulation.
    //        Use the `gondolaCollisionActive` flag.
    return;

    // Clear and rebuild
    destructAndResetCollisions(d, em);
    // buildCollisions(d, d->engineRef, d->collisions, d->gondolaNetworkType);  @TODO: @FIXME: @NOCHECKIN
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
        renderObjsToUnregister.push_back(sim.renderObj);
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
    remainderT += 1.0f;  // @NOTE: the spline is supposed to be calculated at arange [1, 2), hence remainderT getting +1
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
    GondolaSystem_XData::Simulation newSimulation;
    newSimulation.positionT = spawnT;
    d->rom->registerRenderObjects({
            {
                .model = d->rom->getModel("BuilderObj_GondolaNetworkFutsuu", modelOwner, [](){}),
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = guid,
            }
        },
        { &newSimulation.renderObj }
    );
    d->simulations.push_back(newSimulation);
}

void updateSimulation(GondolaSystem_XData* d, size_t simIdx, const float_t& physicsDeltaTime)
{
    GondolaSystem_XData::Simulation& ioSimulation = d->simulations[simIdx];
    ioSimulation.positionT += physicsDeltaTime;
    if (!calculatePositionOnCurveFromT(d, ioSimulation.positionT, ioSimulation.calculatedPositionWorld))
    {
        // Remove simulation if out of range.
        d->rom->unregisterRenderObjects({ ioSimulation.renderObj });
        d->simulations.erase(d->simulations.begin() + simIdx);
    }
}

void GondolaSystem::physicsUpdate(const float_t& physicsDeltaTime)
{
    drawDEBUGCurveVisualization(_data);

    // Spawn simulations.
    _data->simSpawnTimer++;
    if (_data->simSpawnTimer % 100 == 0)
        spawnSimulation(_data, this, getGUID(), 0.0f);

    // Update simulations.
    // @TODO: there should be some kind of timeslicing for this, then update the timestamp of the new calculated point, and in `lateUpdate()` interpolate between the two generated points.
    for (int64_t i = _data->simulations.size() - 1; i >= 0; i--)  // Reverse iteration so that delete can happen.
        updateSimulation(_data, i, physicsDeltaTime);

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
    {
        _data->playerCharWithinRange = false;
        return;
    }
    
    float_t closestDistInRangeSqr = std::numeric_limits<float_t>::max();
    size_t  closestDistSimulationIdx = (size_t)-1;
    for (size_t i = 0; i < _data->simulations.size(); i++)
    {
        auto& simulation = _data->simulations[i];

        float_t currentDistance2 = glm_vec3_distance2(simulation.calculatedPositionWorld, *globalState::playerPositionRef);
        if (currentDistance2 < _data->priorityRange * _data->priorityRange &&
            currentDistance2 < closestDistInRangeSqr)
        {
            closestDistInRangeSqr = currentDistance2;
            closestDistSimulationIdx = i;
        }
    }
    if (closestDistSimulationIdx == (size_t)-1)
        return;  // No position in range was found. Exit.

    readyGondolaInteraction(_data, _em, _data->simulations[closestDistSimulationIdx]);
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
    {
        glm_mat4_identity(sim.renderObj->transformMatrix);
        glm_translate(sim.renderObj->transformMatrix, sim.calculatedPositionWorld);
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
}
