#include "MinecartSystem.h"

#include "RenderObject.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


MinecartSystem::MinecartSystem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{
    if (ds)
        load(*ds);

    _minecartModel                         = _rom->getModel("Minecart", this, [](){});
    _builder_bezierControlPointHandleModel = _rom->getModel("BuilderObj_BezierHandle", this, [](){});
    _renderObjs.push_back(
        _rom->registerRenderObject({
            .model = _builder_bezierControlPointHandleModel,
            .renderLayer = RenderLayer::BUILDER,
            .attachedEntityGuid = getGUID(),
        })
    );
}

MinecartSystem::~MinecartSystem()
{
    for (auto& ro : _renderObjs)
        _rom->unregisterRenderObject(ro);
    _rom->removeModelCallbacks(this);
}

void MinecartSystem::physicsUpdate(const float_t& physicsDeltaTime)
{}

void MinecartSystem::lateUpdate(const float_t& deltaTime)
{}

void MinecartSystem::dump(DataSerializer& ds)
{
    Entity::dump(ds);
}

void MinecartSystem::load(DataSerialized& ds)
{
    Entity::load(ds);
}

void MinecartSystem::loadModelWithName(const std::string& modelName)
{}

void MinecartSystem::createCollisionMeshFromModel()
{}

void MinecartSystem::reportMoved(void* matrixMoved)
{}

void MinecartSystem::renderImGui()
{
    ImGui::Text("HELLO!!!!");

    if (_isDirty)
    {
        if (ImGui::Button("Rebake System"))
        {
            // @TODO: create the baking system

            _isDirty = false;
        }
    }
}
