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

    _cubeModel = _rom->getModel("DevBoxWood");

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
            _isShallowPlanet ? _shallowPlanetMass : false,
            position,
            rotation,
            new btBoxShape(physutil::toVec3(scale * 0.5f)),
            &getGUID()
        );
    _physicsObj->body->setGravity(btVector3(0, 0, 0));

    _shallowPlanetTargetPosition = position;

    _enablePhysicsUpdate = true;
    _enableLateUpdate = _isShallowPlanet;
}

Yosemite::~Yosemite()
{
    _rom->unregisterRenderObject(_renderObj);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    // @TODO: figure out if I need to call `delete _collisionShape;` or not
}

void Yosemite::physicsUpdate(const float_t& physicsDeltaTime)
{
    if (!_isShallowPlanet)
        return;

    _physicsObj->body->setDamping(_shallowPlanetLinDamp, _shallowPlanetAngDamp);

    //
    // Calculate uprightness of this planet if already very straight
    //
    float_t mass = _physicsObj->body->getMass();
    btTransform& myTrans = _physicsObj->body->getWorldTransform();
    _physicsObj->body->applyForce(
        (physutil::toVec3(_shallowPlanetTargetPosition) - myTrans.getOrigin() - _physicsObj->body->getLinearVelocity() * 0.1f) * _shallowPlanetAccel * mass,
        btVector3(0, 0, 0)
    );
    btVector3 transformUp = myTrans.getBasis() * btVector3(0, 1, 0);
    glm::quat toTheTop = glm::quat(physutil::toVec3(transformUp), glm::vec3(0, 1, 0));
    _physicsObj->body->applyTorque(
        btVector3(toTheTop.x, toTheTop.y, toTheTop.z) * _shallowPlanetTorque * mass
    );
}

void Yosemite::lateUpdate(const float_t& deltaTime)
{
    _renderObj->transformMatrix =
        glm::translate(glm::mat4(1.0f), physutil::getPosition(_physicsObj->interpolatedTransform)) *
        glm::toMat4(physutil::getRotation(_physicsObj->interpolatedTransform)) *
        glm::scale(glm::mat4(1.0f), physutil::getScale(_renderObj->transformMatrix));
}

void Yosemite::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpMat4(_renderObj->transformMatrix);
    ds.dumpFloat(_isShallowPlanet);
    ds.dumpFloat(_shallowPlanetLinDamp);
    ds.dumpFloat(_shallowPlanetAngDamp);
    ds.dumpFloat(_shallowPlanetAccel);
    ds.dumpFloat(_shallowPlanetTorque);
    ds.dumpVec3(_treadmillVelocity);
    ds.dumpFloat(_groundedAccelMult);
}

void Yosemite::load(DataSerialized& ds)
{
    Entity::load(ds);

    // V1
    _load_renderTransform = ds.loadMat4();

    // V2
    if (ds.getSerializedValuesCount() >= 5)
    {
        _isShallowPlanet      = ds.loadFloat();
        _shallowPlanetLinDamp = ds.loadFloat();
        _shallowPlanetAngDamp = ds.loadFloat();
        _shallowPlanetAccel   = ds.loadFloat();
        _shallowPlanetTorque  = ds.loadFloat();
    }

    // V3
    if (ds.getSerializedValuesCount() >= 1)
        _treadmillVelocity = ds.loadVec3();

    // V4
    if (ds.getSerializedValuesCount() >= 1)
        _groundedAccelMult = ds.loadFloat();
}

glm::vec3 Yosemite::getTreadmillVelocity()
{
    return physutil::toVec3(_physicsObj->currentTransform.getBasis() * physutil::toVec3(_treadmillVelocity));
}

float_t Yosemite::getGroundedAccelMult()
{
    return _groundedAccelMult;
}

void Yosemite::reportMoved(void* matrixMoved)
{
    //
    // Just completely recreate it (bc it's a static body)
    //
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    glm::mat4 glmTrans = *(glm::mat4*)matrixMoved;
    glm::vec3 position = physutil::getPosition(glmTrans);  // @COPYPASTA
    glm::quat rotation = physutil::getRotation(glmTrans);
    glm::vec3 scale    = physutil::getScale(glmTrans);

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            _isShallowPlanet ? _shallowPlanetMass : false,
            position,
            rotation,
            new btBoxShape(physutil::toVec3(scale * 0.5f)),
            &getGUID()
        );
    _physicsObj->body->setGravity(btVector3(0, 0, 0));

    _shallowPlanetTargetPosition = position;
    
    _enableLateUpdate = _isShallowPlanet;
}

void Yosemite::renderImGui()
{
    ImGui::Text("Change the render object's transform to change the yosemite's physicsobj transform");

    ImGui::Separator();

    if (ImGui::Checkbox("_isShallowPlanet", &_isShallowPlanet))  // @TODO: figure out how to get displacement to work on this buddy!
        reportMoved(&_renderObj->transformMatrix);
    ImGui::DragFloat("_shallowPlanetMass", &_shallowPlanetMass);
    ImGui::DragFloat("_shallowPlanetLinDamp", &_shallowPlanetLinDamp);
    ImGui::DragFloat("_shallowPlanetAngDamp", &_shallowPlanetAngDamp);
    ImGui::DragFloat("_shallowPlanetAccel", &_shallowPlanetAccel);
    ImGui::DragFloat("_shallowPlanetTorque", &_shallowPlanetTorque);
    ImGui::DragFloat3("_shallowPlanetTargetPosition", &_shallowPlanetTargetPosition[0]);

    ImGui::Separator();

    ImGui::DragFloat3("_treadmillVelocity", &_treadmillVelocity[0]);
    ImGui::DragFloat("_groundedAccelMult", &_groundedAccelMult);
}
