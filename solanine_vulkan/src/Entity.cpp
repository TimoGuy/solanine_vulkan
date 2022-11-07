#include "Entity.h"

#include "EntityManager.h"
#include "DataSerialization.h"
#include "GenerateGUID.h"


Entity::Entity(EntityManager* em, DataSerialized* ds) : _em(em)
{
	if (ds == nullptr)
		_guid = generateGUID();
    _em->INTERNALaddEntity(this);
}

Entity::~Entity()
{
    _em->INTERNALdestroyEntity(this);    // @NOTE: the destructor should only be called thru engine->destroyEntity(), not the destructor
    // @NOTE: well, if you're the destroyEntities routine, then go right ahead!
    //        By the way, call me Dmitri.
}

void Entity::dump(DataSerializer& ds)
{
	ds.dumpString(_guid);
}

void Entity::load(DataSerialized& ds)
{
	_guid = ds.loadString();
}
