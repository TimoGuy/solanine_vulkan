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
	ds.loadString(_guid);

    if (_em->INTERNALcheckGUIDCollision(this))
	{
		// Resolve guid collision
		// @NOTE: @INCOMPLETE: this doesn't solve if there are any references that use the guid.
		//                     Hopefully that can be thought out before that issue can happen.
		std::string newGuid = generateGUID();
		std::cerr << "[ENTITY INSERTION ROUTINE]" << std::endl
			<< "WARNING: GUID collision found. GUID regenerated for object with GUID: " << _guid << std::endl
			<< "                                                New regenerated GUID: " << newGuid << std::endl;
		_guid = newGuid;
	}
}
