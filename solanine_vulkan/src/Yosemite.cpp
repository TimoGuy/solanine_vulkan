#include "Yosemite.h"

#include "VulkanEngine.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "InputManager.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/ImGuizmo.h"


Yosemite::Yosemite(VulkanEngine* engine, DataSerialized* ds) : Entity(engine, ds)
{
    if (ds)
        load(*ds);

    _cubeModel = _engine->getModel("cube");

    _renderObj =
        _engine->registerRenderObject({
            .model = _cubeModel,
            .transformMatrix = _tempLoadedRenderTransform,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });
    
    glm::vec3 position;
    glm::vec3 eulerAngles;
    glm::vec3 scale;
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(_renderObj->transformMatrix), glm::value_ptr(position), glm::value_ptr(eulerAngles), glm::value_ptr(scale));

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            position,
            glm::quat(glm::radians(eulerAngles)),
            new btBoxShape(physutil::toVec3(scale * 0.5f))
        );

    _enablePhysicsUpdate = true;
}

Yosemite::~Yosemite()
{
    _engine->unregisterRenderObject(_renderObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    // @TODO: figure out if I need to call `delete _collisionShape;` or not
}

void Yosemite::physicsUpdate(const float_t& physicsDeltaTime)
{
    // @TODO: only update this if the renderobj transform changed (use the _enablePhysicsUpdate flag)
    updatePhysicsObjFromRenderTransform();
}

void Yosemite::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpMat4(_renderObj->transformMatrix);
}

void Yosemite::load(DataSerialized& ds)
{
    Entity::load(ds);
    _tempLoadedRenderTransform = ds.loadMat4();
}

void Yosemite::renderImGui()
{
    ImGui::Text("Change the render object's transform to change the yosemite's physicsobj transform");
}

void Yosemite::updatePhysicsObjFromRenderTransform()
{
    if (physutil::matrixEquals(_renderObj->transformMatrix, _tempLoadedRenderTransform))
        return;
    _tempLoadedRenderTransform = _renderObj->transformMatrix;

    //
    // Just completely recreate it (bc it's a static body)
    //
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    glm::vec3 position;  // @COPYPASTA
    glm::vec3 eulerAngles;
    glm::vec3 scale;
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(_renderObj->transformMatrix), glm::value_ptr(position), glm::value_ptr(eulerAngles), glm::value_ptr(scale));

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            position,
            glm::quat(glm::radians(eulerAngles)),
            new btBoxShape(physutil::toVec3(scale * 0.5f))
        );
}
