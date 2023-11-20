#pragma once

#include <cmath>
#include <string>
#include <cglm/types.h>
class EntityManager;
class DataSerializer;
class DataSerialized;


class Entity  // @TODO: rename this `SimulationObject` or something like that.
{
public:
    Entity(EntityManager* em, DataSerialized* ds);
    virtual ~Entity();
    virtual void simulationUpdate(float_t simDeltaTime) { }        // Gets called once per loop inside simulation thread.
    virtual void dump(DataSerializer& ds) = 0;  // Dumps all the data from the entity to the dataserializer
    virtual void load(DataSerialized& ds) = 0;  // Loads data from the serialized data
    virtual bool processMessage(DataSerialized& message) { return false; }  // Called thru entitymanager::sendMessage if not directly
    virtual std::string getTypeName() = 0;
    std::string& getGUID() { return _guid; }

    virtual void reportMoved(mat4* matrixMoved) { }
    virtual void renderImGui() { }

    bool _isOwned = false;  // This gets set during the creation.

    // @NOTE: you need to manually enable these!
    bool _enableSimulationUpdate = false;

protected:
    EntityManager* _em;

private:
    std::string _guid;

    friend class EntityManager;
};
