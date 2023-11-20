#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>

class Entity;
class DataSerialized;
namespace physengine
{
	void runPhysicsEngineAsync();
}


class EntityManager  // @TODO: make this a namespace instead of an object. This way it can be referenced globally.
{
private:
	EntityManager() = default;
	~EntityManager();

public:
	void INTERNALsimulationUpdate(float_t simDeltaTime);
private:
    void INTERNALaddEntity(Entity* entity);
	void INTERNALdestroyEntity(Entity* entity);
	void INTERNALaddRemoveRequestedEntities();
	bool INTERNALcheckGUIDCollision(Entity* entity);

public:
	Entity* getEntityViaGUID(const std::string& guid);
	bool sendMessage(const std::string& guid, DataSerialized& message);
	void destroyEntity(Entity* entity);    // Do not use the destructor or INTERNALdestroyEntity(), use this function!
	void destroyOwnedEntity(Entity* entity);    // DITTO as above.

private:
	std::vector<Entity*> _entities;
	std::deque<Entity*> _entitiesToAddQueue;
	std::deque<Entity*> _entitiesToDestroyQueue;
	std::mutex _entityCollectionMutex;
	bool _flushEntitiesToDestroyRoutine = false;

	friend class VulkanEngine;
	friend class Entity;
	friend void physengine::runPhysicsEngineAsync();
};
