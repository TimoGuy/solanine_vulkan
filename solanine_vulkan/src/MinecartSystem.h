#pragma once

#include "Entity.h"
#include "Imports.h"
namespace vkglTF { struct Model; }
class     EntityManager;
struct    RenderObject;
class     RenderObjectManager;
struct    RegisteredPhysicsObject;


class MinecartSystem : public Entity
{
public:
    static const std::string TYPE_NAME;

    MinecartSystem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~MinecartSystem();

    void physicsUpdate(const float_t& physicsDeltaTime);
    void lateUpdate(const float_t& deltaTime);

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; }

    void loadModelWithName(const std::string& modelName);
    void createCollisionMeshFromModel();

    void reportMoved(void* matrixMoved);
    void renderImGui();

private:
    vkglTF::Model*                        _minecartModel;
    vkglTF::Model*                        _builder_bezierControlPointHandleModel;  // @NOTE: put the renderobjects created from this in the builder section
    std::vector<RenderObject*>            _renderObjs;
    RenderObjectManager*                  _rom = nullptr;
    std::vector<RegisteredPhysicsObject*> _physicsObjs;  // Holds the minecarts and the rail physics objects

    glm::mat4 _load_transform = glm::mat4(1.0f);  // @NOTE: try to ignore scale

    //
    // Minecart system simulation
    //
    struct Path  // @NOTE: atm this system can only be traversed one way. In the future two-way traversal may be necessary but let's just keep it at this limitation for initial buildup purposes. Note there may not even be a need to improve this system other than bugfixes.  -Timo 2022/12/18
    {
        std::vector<glm::vec3> controlPoints;     // @NOTE: this is handled using bezier curves, hence the usage of control points
        float_t                pathScale;         // Baked: This is multiplied by `(distanceTraveled - startPathDistance)`
        float_t                startPathDistance; // Baked: note that this is the "real" distance, as in, it's not multiplied by `pathScale` and it's supposed to be used with `distanceTraveled`
    };

    struct PathSwitch
    {
        bool   isOn;
        size_t pathIndex;     // Path index being referenced with this switch.
        size_t toPathIndex0;  // Path index to go to if the switch is off.
        size_t toPathIndex1;  // Path index to go to if the switch is on.
    };

    struct MinecartSimulation
    {
        bool    isOnAPath;         // Once the minecart simulation finishes out all paths it simulates thru, it will fall off and just do a freefall simulation (where `isOnAPath == false`).
        size_t  pathIndex;         // The currect Path that is being traveled down. This index is used to do a calculation on the exact position of the minecart.
        float_t distanceTraveled;  // @NOTE: this is the unscaled number where `(speed * speedMultiplier)` adds to.
        float_t speedMultiplier;   // This value gets tweaked by the slope that the minecart is sitting on with the rails. Of course a steeper slope it's sitting at will make it go faster, though it may only increase in speed at the rate that `speedChangeSpeed` allows for. 
    };

    struct MinecartSimulationSettings
    {
        float_t speed;             // Constant value of base speed of the minecarts.
        float_t speedChangeSpeed;  // The speed at which `speedMultiplier` can change. This is effectively the "acceleration" of `speedMultiplier`.
    };

    std::vector<Path>               _paths;
    std::vector<PathSwitch>         _switches;
    std::vector<MinecartSimulation> _minecartSims;
    MinecartSimulationSettings      _minecartSimSettings;  // This is essentially the main tweak props other than configuring the actual simulation

    //
    // Tweak Props
    //
};
