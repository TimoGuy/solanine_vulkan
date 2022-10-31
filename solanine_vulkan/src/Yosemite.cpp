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
    _cubeModel = _engine->getModel("cube");

    _renderObj =
        _engine->registerRenderObject({
            .model = _cubeModel,
            .transformMatrix = glm::mat4(1.0f),
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    if (ds)
        load(*ds);
    
    glm::vec3 position;
    glm::vec3 eulerAngles;
    glm::vec3 scale;
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(_renderObj->transformMatrix), glm::value_ptr(position), glm::value_ptr(eulerAngles), glm::value_ptr(scale));

    _collisionShape = new btBoxShape(physutil::toVec3(scale * 0.5f));
    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            position,
            glm::quat(eulerAngles),
            _collisionShape
        );

    updatePhysicsObjFromRenderTransform();

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
    _renderObj->transformMatrix = ds.loadMat4();
}

void Yosemite::renderImGui()
{
    ImGui::Text("Change the render object's transform to change the yosemite's physicsobj transform");
}

void Yosemite::updatePhysicsObjFromRenderTransform()
{
    return;


	btTransform trans;
	trans.setFromOpenGLMatrix(glm::value_ptr(_renderObj->transformMatrix));
    _physicsObj->body->setWorldTransform(trans);

    _collisionShape = new btBoxShape(physutil::toVec3(physutil::getScale(_renderObj->transformMatrix) * 0.5f));
    _physicsObj->body->setCollisionShape(_collisionShape);
}
