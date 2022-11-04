#pragma once

#include <vector>
#include <queue>

class Entity;


class EntityManager
{
private:
	EntityManager() = default;
	~EntityManager();

    void INTERNALaddEntity(Entity* entity);
	void INTERNALdestroyEntity(Entity* entity);
	void INTERNALaddRemoveRequestedEntities();

public:
	void destroyEntity(Entity* entity);    // Do not use the destructor or INTERNALdestroyEntity(), use this function!

private:
	std::vector<Entity*> _entities;
	std::deque<Entity*> _entitiesToAddQueue;
	std::deque<Entity*> _entitiesToDestroyQueue;
	bool _flushEntitiesToDestroyRoutine = false;

	friend class VulkanEngine;
	friend class Entity;
};
