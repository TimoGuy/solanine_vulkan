#include "Entity.h"

#include "VulkanEngine.h"
#include "DataSerialization.h"


uint32_t randomChar()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, 255);
	return dis(gen);
}

std::string generateHex(const uint32_t numChars)
{
	std::stringstream ss;
	for (uint32_t i = 0; i < numChars; i++)
	{
		const auto rc = randomChar();
		std::stringstream hexstream;
		hexstream << std::hex << rc;
		auto hex = hexstream.str();
		ss << (hex.length() < 2 ? '0' + hex : hex);
	}
	return ss.str();
}


Entity::Entity(VulkanEngine* engine, DataSerialized* ds) : _engine(engine)
{
	if (ds == nullptr)
		_guid = generateHex(32);
    _engine->INTERNALaddEntity(this);
}

Entity::~Entity()
{
    _engine->INTERNALdestroyEntity(this);    // @NOTE: the destructor should only be called thru engine->destroyEntity(), not the destructor
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
