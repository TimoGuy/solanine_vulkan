#include "Yosemite.h"

#include "ImportGLM.h"
#include "RenderObject.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "InputManager.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/ImGuizmo.h"


Yosemite::Yosemite(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{
    if (ds)
        load(*ds);

    _cubeModel = _rom->getModel("devBoxWood");

    _renderObj =
        _rom->registerRenderObject({
            .model = _cubeModel,
            .transformMatrix = _load_renderTransform,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    glm::vec3 position = physutil::getPosition(_renderObj->transformMatrix);  // @COPYPASTA
    glm::quat rotation = physutil::getRotation(_renderObj->transformMatrix);
    glm::vec3 scale    = physutil::getScale(_renderObj->transformMatrix);

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            position,
            rotation,
            new btBoxShape(physutil::toVec3(scale * 0.5f))
        );

    _enablePhysicsUpdate = true;
}

Yosemite::~Yosemite()
{
    _rom->unregisterRenderObject(_renderObj);
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
    _load_renderTransform = ds.loadMat4();
}

void Yosemite::renderImGui()
{
    ImGui::Text("Change the render object's transform to change the yosemite's physicsobj transform");
}

#ifdef _DEVELOP
void Yosemite::updatePhysicsObjFromRenderTransform()
{
    if (physutil::matrixEquals(_renderObj->transformMatrix, _load_renderTransform))
        return;
    _load_renderTransform = _renderObj->transformMatrix;  // @HACK: this isn't production code... that's why it's a _load_* variable despite being used for more than that in actuality

    //
    // Just completely recreate it (bc it's a static body)
    //
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    glm::vec3 position = physutil::getPosition(_renderObj->transformMatrix);  // @COPYPASTA
    glm::quat rotation = physutil::getRotation(_renderObj->transformMatrix);
    glm::vec3 scale    = physutil::getScale(_renderObj->transformMatrix);

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            position,
            rotation,
            new btBoxShape(physutil::toVec3(scale * 0.5f))
        );
}
#endif
