#include "EntityManager.h"

#include <iostream>
#include "Entity.h"
#include "GenerateGUID.h"
#include "DataSerialization.h"
#include "PhysicsEngine.h"


EntityManager::~EntityManager()
{
    _flushEntitiesToDestroyRoutine = true;
    for (int32_t i = (int32_t)_entities.size() - 1; i >= 0; i--)  // @NOTE: have to use this backwards iterator for some reason bc `delete` keyword doesn't play well with `for (auto e : _entities)` like... why?
		delete _entities[i];
}

void EntityManager::INTERNALphysicsUpdate(const float_t& deltaTime)
{
	std::lock_guard<std::mutex> lg(_entityCollectionMutex);

	// @TODO: multithread this sucker!
	for (auto it = _entities.begin(); it != _entities.end(); it++)
	{
		Entity* ent = *it;
		if (ent->_enablePhysicsUpdate)
			ent->update(deltaTime);
	}
}

void EntityManager::update(const float_t& deltaTime)
{
	// Interpolate all physics objects
	physengine::setPhysicsObjectInterpolation(physengine::getPhysicsAlpha());

	// @TODO: multithread this sucker!
	for (auto it = _entities.begin(); it != _entities.end(); it++)
	{
		Entity* ent = *it;
		if (ent->_enableUpdate)
			ent->update(deltaTime);
	}
}

void EntityManager::lateUpdate(const float_t& deltaTime)
{
	// @COPYPASTA
	// @TODO: multithread this sucker!
	for (auto it = _entities.begin(); it != _entities.end(); it++)
	{
		Entity* ent = *it;
		if (ent->_enableLateUpdate)
			ent->lateUpdate(deltaTime);
	}
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
	std::lock_guard<std::mutex> lg(_entityCollectionMutex);

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

bool EntityManager::INTERNALcheckGUIDCollision(Entity* entity)
{
	// @IMPROVE: add short circuit for the collision evaluation
	std::string guid = entity->getGUID();
	bool collision = false;
	for (auto& ent : _entitiesToDestroyQueue)
		if (ent != entity)
			collision |= (ent->getGUID() == guid);
	for (auto& ent : _entitiesToAddQueue)
		if (ent != entity)
			collision |= (ent->getGUID() == guid);
	for (auto& ent : _entities)
		if (ent != entity)
			collision |= (ent->getGUID() == guid);
	return collision;
}

Entity* EntityManager::getEntityViaGUID(const std::string& guid)
{
	for (auto& ent : _entities)
	{
		if (ent->getGUID() == guid)
		{
			return ent;
		}
	}
	return nullptr;
}

bool EntityManager::sendMessage(const std::string& guid, DataSerialized& message)
{
	if (Entity* ent = getEntityViaGUID(guid))
	{
		return ent->processMessage(message);
	}

	std::cerr << "[ENTITY MGR SEND MESSAGE]" << std::endl
		<< "WARNING: message \"" << message.loadString() << "\" was not sent bc there was no entity with guid " << guid << " found." << std::endl;  // @TODO: make message.tostring() so that you can see what message didn't go thru

	return false;
}

void EntityManager::destroyEntity(Entity* entity)
{
	_entitiesToDestroyQueue.push_back(entity);
}
