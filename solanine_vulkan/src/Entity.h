#pragma once

#include <cmath>
#include <string>
class EntityManager;
class DataSerializer;
class DataSerialized;


class Entity
{
public:
    Entity(EntityManager* em, DataSerialized* ds);
    virtual ~Entity();
    virtual void update(const float_t& deltaTime) { }    // Gets called once per frame
    virtual void physicsUpdate(const float_t& physicsDeltaTime) { }        // Gets called once per physics calculation
    virtual void dump(DataSerializer& ds) = 0;  // Dumps all the data from the entity to the dataserializer
    virtual void load(DataSerialized& ds) = 0;  // Loads data from the serialized data
    virtual std::string getTypeName() = 0;
    std::string getGUID() { return _guid; }

    virtual void renderImGui() { }

    // @NOTE: you need to manually enable these!
    bool _enableUpdate = false,
         _enablePhysicsUpdate = false;

protected:
    EntityManager* _em;

private:
    std::string _guid;

    friend class EntityManager;
};
