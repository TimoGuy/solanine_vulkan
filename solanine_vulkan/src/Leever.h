#pragma once

#include "Entity.h"
#include "Imports.h"
namespace vkglTF { struct Model; }
class     EntityManager;
struct    RenderObject;
class     RenderObjectManager;
struct    RegisteredPhysicsObject;


class Leever : public Entity
{
public:
    static const std::string TYPE_NAME;
    std::string getTypeName() { return TYPE_NAME; }

    Leever(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~Leever();

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);

    void reportMoved(void* matrixMoved);
    void renderImGui();

private:
    vkglTF::Model*           _model;
    RenderObject*            _renderObj  = nullptr;
    RenderObjectManager*     _rom        = nullptr;
    RegisteredPhysicsObject* _physicsObj = nullptr;

    glm::mat4 _load_transform = glm::mat4(1.0f);

    // Callbacks
    std::function<void(btPersistentManifold*, bool amIB)> _onCollisionStayFunc;
    void onCollisionStay(btPersistentManifold* manifold, bool amIB);

    // Tweak Props
    std::string _messageReceiverGuid;  // @NOTE: this is the object that the switch will affect.
    int32_t     _receiverPortNumber;   // This is to distinguish this Leever in case if there are multiple (i.e. for MinecartSystems this number is necessary).
    bool        _isOn = false;
};
