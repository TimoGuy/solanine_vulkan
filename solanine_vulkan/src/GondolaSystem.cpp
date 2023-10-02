#include "GondolaSystem.h"

#include <Jolt/Jolt.h>
#include "EntityManager.h"
#include "RenderObject.h"
#include "HotswapResources.h"
#include "SceneManagement.h"
#include "GlobalState.h"
#include "VoxelField.h"


struct GondolaSystem_XData
{
    RenderObjectManager*       rom;
    std::vector<RenderObject*> cartRenderObjs;  // Render objs for all the carts in the scene.
    std::vector<VoxelField*>   collisions;  // Collision objects for the most nearby train to the player character.

    enum class GondolaNetworkType
    {
        NONE = 0,
        FUTSUU,
        JUNKYUU,
        KAISOKU,
        TOKKYUU,
    };
    GondolaNetworkType initializedInteractiveGondolaType = GondolaNetworkType::NONE;

    bool               playerCharWithinRange = false;
    float_t            priorityRange = 200.0f;

    struct Simulation
    {
        vec3 position;
        GondolaNetworkType type;
    };
    std::vector<Simulation> simulations;

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
};

void buildCollisions(GondolaSystem_XData::GondolaNetworkType networkType)
{
    switch (networkType)
    {
        case GondolaSystem_XData::GondolaNetworkType::NONE:
            return;
            
        case GondolaSystem_XData::GondolaNetworkType::FUTSUU:
        {
            std::vector<Entity*> ents;
            scene::loadPrefab("res/prefabs/gondola_collision_futsuu.hunk", engine, ents);  // Supposed to contain all the cars for collision (even though there are repeats in the collision data).
            // @TODO: start here!!!!!!! Get the `ents` into an ownership list (there should already be something here... plus you need to cast it).
        } return;

        case GondolaSystem_XData::GondolaNetworkType::JUNKYUU:
            return;

        case GondolaSystem_XData::GondolaNetworkType::KAISOKU:
            return;

        case GondolaSystem_XData::GondolaNetworkType::TOKKYUU:
        {

        } return;
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
    // Check that the collisions are correct.
    if (d->initializedInteractiveGondolaType != simulation.type)
    {
        // Clear and rebuild
        destructAndResetCollisions(d, em);

        d->initializedInteractiveGondolaType = simulation.type;
    }
}

GondolaSystem::GondolaSystem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new GondolaSystem_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;

    if (ds)
        load(*ds);
}

GondolaSystem::~GondolaSystem()
{
    hotswapres::removeOwnedCallbacks(this);

    _data->rom->unregisterRenderObjects(_data->cartRenderObjs);
    _data->cartRenderObjs.clear();

    destructAndResetCollisions(_data, _em);

    delete _data;
}

void GondolaSystem::physicsUpdate(const float_t& physicsDeltaTime)
{
    if (!_data->timeslicing.checkTimeslice())
        return;  // Exit bc not timesliced position.

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

        float_t currentDistance2;
        if ((currentDistance2 = glm_vec3_distance2(simulation.position, *globalState::playerPositionRef)) < _data->priorityRange * _data->priorityRange)
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
}

void GondolaSystem::reportMoved(mat4* matrixMoved)
{
}

void GondolaSystem::renderImGui()
{
}
