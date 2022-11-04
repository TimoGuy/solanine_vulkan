#include "EntityManager.h"

#include <iostream>
#include "Entity.h"


EntityManager::~EntityManager()
{
    _flushEntitiesToDestroyRoutine = true;
    for (int32_t i = (int32_t)_entities.size() - 1; i >= 0; i--)  // @NOTE: have to use this backwards iterator for some reason bc `delete` keyword doesn't play well with `for (auto e : _entities)` like... why?
		delete _entities[i];
}

void EntityManager::INTERNALaddEntity(Entity* entity)
{
    _entitiesToAddQueue.push_back(entity);    // @NOTE: this only requests that the entity get added into the system
}

void EntityManager::INTERNALdestroyEntity(Entity* entity)
{
    if (!_flushEntitiesToDestroyRoutine)
	{
		// Still must destroy this entity, but give a very nasty warning message
		std::cout << "[DESTROY ENTITY]" << std::endl
			<< "WARNING: what you're doing is very wrong." << std::endl
			<< "         Don't use the destructor for entities, instead use destroyEntity()." << std::endl
			<< "         Crashes could easily happen." << std::endl;
	}

	_entities.erase(
		std::remove(
			_entities.begin(),
			_entities.end(),
			entity
		),
		_entities.end()
	);
}

void EntityManager::INTERNALaddRemoveRequestedEntities()
{
	// Remove entities requested to be removed
	_flushEntitiesToDestroyRoutine = true;

	for (auto it = _entitiesToDestroyQueue.begin(); it != _entitiesToDestroyQueue.end(); it++)
		delete (*it);
	_entitiesToDestroyQueue.clear();

	_flushEntitiesToDestroyRoutine = false;

	// Add entities requested to be added
	for (auto it = _entitiesToAddQueue.begin(); it != _entitiesToAddQueue.end(); it++)
		_entities.push_back(*it);
	_entitiesToAddQueue.clear();
}

void EntityManager::destroyEntity(Entity* entity)
{
	_entitiesToDestroyQueue.push_back(entity);
}
