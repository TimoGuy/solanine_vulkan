#include "MinecartSystem.h"

#include "RenderObject.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


MinecartSystem::MinecartSystem(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{}

MinecartSystem::~MinecartSystem()
{}

void MinecartSystem::physicsUpdate(const float_t& physicsDeltaTime)
{}

void MinecartSystem::lateUpdate(const float_t& deltaTime)
{}

void MinecartSystem::dump(DataSerializer& ds)
{}

void MinecartSystem::load(DataSerialized& ds)
{}

void MinecartSystem::loadModelWithName(const std::string& modelName)
{}

void MinecartSystem::createCollisionMeshFromModel()
{}

void MinecartSystem::reportMoved(void* matrixMoved)
{}

void MinecartSystem::renderImGui()
{}
