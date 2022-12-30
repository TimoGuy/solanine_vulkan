#include "Leever.h"


Leever::Leever(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{
    if (ds)
        load(*ds);

    _model = _rom->getModel("Leever");

    _renderObj =
        _rom->registerRenderObject({
            .model = _model,
            .transformMatrix = _load_transform,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            physutil::getPosition(_load_transform),
            physutil::getRotation(_load_transform),
            new btBoxShape({ 1, 1, 1 }),
            &getGuid()
        );

    _onCollisionStayFunc =
        [&](btPersistentManifold* manifold, bool amIB) { onCollisionStay(manifold, amIB); };
    _physicsObj->onCollisionStayCallback = &_onCollisionStayFunc;
}

Leever::~Leever()
{
    _rom->unregisterRenderObject(_renderObj);
    _rom->removeModelCallbacks(this);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);
}

void Leever::dump(DataSerializer& ds)
{
    Entity::dump(ds);
}

void Leever::load(DataSerialized& ds)
{
    Entity::load(ds);
}

void Leever::reportMoved(void* matrixMoved)
{
    _physicsObj->reportMoved(matrixMoved);
}

void Leever::renderImGui()
{
    // @TODO: add in the imgui stuff for the reciever guid, port num, and the ison message


    /*
    DataSerializer ds;
    ds.dumpString("event_attacked");
    ds.dumpVec3(pushDirection);

    DataSerialized dsd = ds.getSerializedData();
    if (_em->sendMessage(*(std::string*)collisionObj->getUserPointer(), dsd))
    {
        // velocity = -pushDirection * 10.0f;
    }
    */
}

void Leever::onCollisionStay(btPersistentManifold* manifold, bool amIB)
{
    // @TODO: create the collision interface for this object! (note it should switch the leever's position with this and have a bit of a debounce to play nice)






    // @NOTE: this was causing lots of movement popping
    /*_groundContactNormal = glm::vec3(0.0f);
    size_t numContacts = (size_t)manifold->getNumContacts();
    for (int32_t i = 0; i < numContacts; i++)
    {
        auto contact = manifold->getContactPoint(i);
        auto contactNormal = contact.m_normalWorldOnB * (amIB ? -1.0f : 1.0f);
        bool isGroundContactNormal = contactNormal.y() > glm::cos(glm::radians(47.0f));
        if (isGroundContactNormal)
        {
            _onGround = true;
            _groundContactNormal += physutil::toVec3(contactNormal);
        }
    }
    
    if (_onGround)
        _groundContactNormal = glm::normalize(_groundContactNormal);*/
}
