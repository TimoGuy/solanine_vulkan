#include "Leever.h"

#include "RenderObject.h"
#include "EntityManager.h"
#include "PhysicsEngine.h"
#include "VkglTFModel.h"
#include "AudioEngine.h"
#include "DataSerialization.h"
#include "Debug.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


Leever::Leever(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{
    if (ds)
        load(*ds);

    _model = _rom->getModel("Leever", this, [](){});

    _renderObj =
        _rom->registerRenderObject({
            .model = _model,
            .transformMatrix = _load_transform,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    const glm::vec3 toff(0, -3, 0);
    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            physutil::getPosition(_load_transform) - toff,
            physutil::getRotation(_load_transform),
            new btBoxShape({ 2, 3, 1 }),
            &getGUID()
        );
    _physicsObj->transformOffset = toff;

    /*_onCollisionStayFunc =
        [&](btPersistentManifold* manifold, bool amIB) { onCollisionStay(manifold, amIB); };
    _physicsObj->onCollisionStayCallback = &_onCollisionStayFunc;*/

    _enableUpdate = true;
}

Leever::~Leever()
{
    _rom->unregisterRenderObject(_renderObj);
    _rom->removeModelCallbacks(this);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);
}

void Leever::update(const float_t& deltaTime)
{
    _attackedDebounceTimer -= deltaTime;
    _renderObj->transformMatrix = _physicsObj->interpolatedTransform;
}

void Leever::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpMat4(_renderObj->transformMatrix);
    ds.dumpString(_messageReceiverGuid.empty() ? "None" : _messageReceiverGuid);
    ds.dumpFloat((float_t)_receiverPortNumber);
    ds.dumpFloat((float_t)_isOn);
}

void Leever::load(DataSerialized& ds)
{
    Entity::load(ds);
    _load_transform      = ds.loadMat4();
    _messageReceiverGuid = ds.loadString();
    _receiverPortNumber  = (int32_t)ds.loadFloat();
    _isOn                = (bool)ds.loadFloat();
}

bool Leever::processMessage(DataSerialized& message)
{
    auto eventName = message.loadString();
    if (eventName == "event_attacked")
    {
        if (_attackedDebounceTimer > 0.0f)
            return false;

        _isOn = !_isOn;
        sendUpdateIsOnMessage();

        AudioEngine::getInstance().playSoundFromList({
            "res/sfx/wip_bonk.ogg",
        });

        _attackedDebounceTimer = _attackedDebounce;

        return true;
    }

    std::cout << "[LEEVER ENT PROCESS MESSAGE]" << std::endl
        << "WARNING: message event name " << eventName << " unknown implementation" << std::endl;

    return false;
}

void Leever::reportMoved(void* matrixMoved)
{
    _physicsObj->reportMoved(
        glm::translate(*(glm::mat4*)matrixMoved, -_physicsObj->transformOffset),
        false
    );
}

void Leever::renderImGui()
{
    ImGui::DragFloat3("_physicsObj->transformOffset", &_physicsObj->transformOffset[0]);

    ImGui::InputText("_messageReceiverGuid", &_messageReceiverGuid);
    if (!_em->getEntityViaGUID(_messageReceiverGuid))
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Entity with this GUID does not exist.");
    
    ImGui::InputInt("_receiverPortNumber", &_receiverPortNumber);
    
    if (ImGui::Checkbox("_isOn", &_isOn))
        sendUpdateIsOnMessage();
}

void Leever::sendUpdateIsOnMessage()
{
    DataSerializer ds;
    ds.dumpString("event_update_isOn");
    ds.dumpFloat((float_t)_receiverPortNumber);
    ds.dumpFloat((float_t)_isOn);

    DataSerialized dsd = ds.getSerializedData();
    if (!_em->sendMessage(_messageReceiverGuid, dsd))
    {
        debug::pushDebugMessage({
            .message = "ERROR: message `event_update_isOn` sending failed!",
            .type = 2,
            });
    }
}

/*
void Leever::onCollisionStay(btPersistentManifold* manifold, bool amIB)
{
    // @TODO: create the collision interface for this object! (note it should switch the leever's position with this and have a bit of a debounce to play nice)
    //        Or you could alternatively set up a receiver for the sword slashing event that the player sends to objects! (This will probs be the better option too btw)





    // @NOTE: this was causing lots of movement popping
    _groundContactNormal = glm::vec3(0.0f);
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
        _groundContactNormal = glm::normalize(_groundContactNormal);
}
*/
