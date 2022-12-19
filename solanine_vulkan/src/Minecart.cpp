#include "Minecart.h"

#include "RenderObject.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


Minecart::Minecart(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{}

Minecart::~Minecart()
{}

void Minecart::physicsUpdate(const float_t& physicsDeltaTime)
{}

void Minecart::lateUpdate(const float_t& deltaTime)
{}

void Minecart::dump(DataSerializer& ds)
{}

void Minecart::load(DataSerialized& ds)
{}

void Minecart::loadModelWithName(const std::string& modelName)
{}

void Minecart::createCollisionMeshFromModel()
{}

void Minecart::reportMoved(void* matrixMoved)
{}

void Minecart::renderImGui()
{}
