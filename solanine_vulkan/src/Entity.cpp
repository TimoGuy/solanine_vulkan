#include "Entity.h"

#include "VulkanEngine.h"


Entity::Entity(VulkanEngine* engine) : engine(engine)
{
    engine->INTERNALaddEntity(this);
}

Entity::~Entity()
{
    engine->INTERNALdestroyEntity(this);    // @NOTE: the destructor should only be called thru engine->destroyEntity(), not the destructor
    // @NOTE: well, if you're the destroyEntities routine, then go right ahead!
    //        By the way, call me Dmitri.
}
