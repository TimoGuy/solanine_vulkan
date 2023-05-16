#pragma once

#include <string>
#include <vector>
#include <queue>

class Entity;
class DataSerialized;


class EntityManager
{
private:
	EntityManager() = default;
	~EntityManager();

public:
	void INTERNALphysicsUpdate(const float_t& deltaTime);
private:
	void update(const float_t& deltaTime);
	void lateUpdate(const float_t& deltaTime);

    void INTERNALaddEntity(Entity* entity);
	void INTERNALdestroyEntity(Entity* entity);
	void INTERNALaddRemoveRequestedEntities();
	bool INTERNALcheckGUIDCollision(Entity* entity);

public:
	Entity* getEntityViaGUID(const std::string& guid);
	bool sendMessage(const std::string& guid, DataSerialized& message);
	void destroyEntity(Entity* entity);    // Do not use the destructor or INTERNALdestroyEntity(), use this function!

private:
	std::vector<Entity*> _entities;
	std::deque<Entity*> _entitiesToAddQueue;
	std::deque<Entity*> _entitiesToDestroyQueue;
	bool _flushEntitiesToDestroyRoutine = false;

	friend class VulkanEngine;
	friend class Entity;
};
