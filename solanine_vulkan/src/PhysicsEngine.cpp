#include "PhysicsEngine.h"

#include <cmath>
#include "VulkanEngine.h"
#include "Entity.h"
#include "VkInitializers.h"


PhysicsEngine& PhysicsEngine::getInstance()
{
	static PhysicsEngine instance;
	return instance;
}

void PhysicsEngine::initialize(VulkanEngine* engine)
{
	std::cout << "[INITIALIZING PHYSICS ENGINE]" << std::endl;    // @NOTE: this is gonna spit out a bunch of verbose console crap
	_engine = engine;

	//
	// Initialize MT task scheduler
	//
	_mainTaskScheduler = btCreateDefaultTaskScheduler();   // @NOTE: this is a multithreaded scheduler... should be based off Win32 or pthreads
	if (!_mainTaskScheduler)
	{
		std::cerr << "[CREATING PHYSICS TASK SCHEDULER]" << std::endl
			<< "ERROR: creating the default task scheduler returned null." << std::endl;
		return;
	}
	_mainTaskScheduler->setNumThreads(_mainTaskScheduler->getMaxNumThreads());
	btSetTaskScheduler(_mainTaskScheduler);

	//
	// Create empty dynamics world
	//
	btDefaultCollisionConstructionInfo cci;
	cci.m_defaultMaxPersistentManifoldPoolSize = 80000;
	cci.m_defaultMaxCollisionAlgorithmPoolSize = 80000;
	_collisionConfiguration = new btDefaultCollisionConfiguration(cci);

	_dispatcher = new btCollisionDispatcherMt(_collisionConfiguration, 40);
	_broadphase = new btDbvtBroadphase();

	btConstraintSolverPoolMt* solverPool;
	{
		btConstraintSolver* solvers[BT_MAX_THREAD_COUNT];
		int maxThreadCount = BT_MAX_THREAD_COUNT;
		for (int i = 0; i < maxThreadCount; ++i)
		{
			solvers[i] = new btSequentialImpulseConstraintSolver();    // @NOTE: Pool solvers shouldn't be parallel solvers, we don't allow that kind of nested parallelism because of performance issues  -Bullet3 example project
		}
		solverPool = new btConstraintSolverPoolMt(solvers, maxThreadCount);
		_solver = solverPool;
	}

	btSequentialImpulseConstraintSolverMt* solverMt = new btSequentialImpulseConstraintSolverMt();
	btDiscreteDynamicsWorld* world = new btDiscreteDynamicsWorldMt(_dispatcher, _broadphase, solverPool, solverMt, _collisionConfiguration);
	_dynamicsWorld = world;
	btAssert(btGetTaskScheduler() != NULL);

	//_dynamicsWorld->setInternalTickCallback(profileBeginCallback, NULL, true);
	//_dynamicsWorld->setInternalTickCallback(profileEndCallback, NULL, false);
	_dynamicsWorld->getSolverInfo().m_solverMode = SOLVER_SIMD | SOLVER_USE_WARMSTARTING;
	_dynamicsWorld->getSolverInfo().m_numIterations = 10;    // @NOTE: 10 is probably good, but Unity engine only uses 6 *shrug*
	_dynamicsWorld->setGravity(btVector3(0, -100, 0));
	_gravityStrength = _dynamicsWorld->getGravity().length();

	//
	// Reserve the transforms
	//
	_physicsObjects.reserve(PHYSICS_OBJECTS_MAX_CAPACITY);
	_transformsBuffer =
		_engine->createBuffer(
			sizeof(GPUObjectData) * PHYSICS_OBJECTS_MAX_CAPACITY,  // Pray that GPUObjectData doesn't change!
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU
		);

	// Allocate
	VkDescriptorSetAllocateInfo objectSetAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = _engine->_descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &_engine->_objectSetLayout,  // Pray this doesn't change!
	};
	vkAllocateDescriptorSets(_engine->_device, &objectSetAllocInfo, &_transformsDescriptor);
	
	// Write
	VkDescriptorBufferInfo transformsBufferInfo = {
		.buffer = _transformsBuffer._buffer,
		.offset = 0,
		.range = sizeof(GPUObjectData) * PHYSICS_OBJECTS_MAX_CAPACITY,
	};
	VkWriteDescriptorSet transformsWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _transformsDescriptor, &transformsBufferInfo, 0);
	vkUpdateDescriptorSets(_engine->_device, 1, &transformsWrite, 0, nullptr);
}

void PhysicsEngine::update(float_t deltaTime, std::vector<Entity*>* entities)    // https://gafferongames.com/post/fix_your_timestep/
{
	constexpr float_t physicsDeltaTime = 0.02f;    // 50fps
	_accumulatedTimeForPhysics += deltaTime;

	for (; _accumulatedTimeForPhysics >= physicsDeltaTime; _accumulatedTimeForPhysics -= physicsDeltaTime)
	{
		for (auto it = entities->begin(); it != entities->end(); it++)    // @IMPROVEMENT: this could be multithreaded if we're just smart by how we do the physicsupdates
		{
			Entity* ent = *it;
			if (ent->_enablePhysicsUpdate)
				ent->physicsUpdate(physicsDeltaTime);
		}

		// @IMPROVEMENT: look into how bullet's step simulation does with interpolation and how we can rely on that instead of the system here... it could be good especially if it's deterministic seeming. The only problem really seems to be what we'd put in for the deltaTime of physicsUpdate() for all the objects
		_dynamicsWorld->stepSimulation(physicsDeltaTime, 0);    // @NOTE: only want the step simulation to happen once at a time
	}

	// Interpolate the positions of all physics objects
	const float_t physicsAlpha = _accumulatedTimeForPhysics / physicsDeltaTime;
#ifdef _DEVELOP
	// @TODO: Make a playmode flag
	//if (!playMode)
	//	physicsAlpha = 1.0f;
#endif
	
	void* objectData;
	vmaMapMemory(_engine->_allocator, _transformsBuffer._allocation, &objectData);   // And what happens if you overwrite the memory during a frame? Well, idk, but it shouldn't be too bad for debugging purposes I think  -Timo 2022/10/24
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;    // @IMPROVE: perhaps multithread this? Or only update when the object moves?
	for (size_t i = 0; i < _physicsObjects.size(); i++)    // @IMPROVEMENT: @TODO: oh man, this should definitely be multithreaded. However, taskflow doesn't seem like the best choice.  @TOOD: look into the c++11 multithreaded for loop
	{
		calculateInterpolatedTransform(_physicsObjects[i], physicsAlpha);
		_physicsObjects[i].body->getWorldTransform().getOpenGLMatrix(glm::value_ptr(objectSSBO[i].modelMatrix));  // Extra Dmitri in this one
	}
	vmaUnmapMemory(_engine->_allocator, _transformsBuffer._allocation);
}

void PhysicsEngine::cleanup()
{
	if (_vertexBufferCreated)
		vmaDestroyBuffer(_engine->_allocator, _vertexBuffer._buffer, _vertexBuffer._allocation);
	vmaDestroyBuffer(_engine->_allocator, _transformsBuffer._buffer, _transformsBuffer._allocation);
	delete _mainTaskScheduler;    // @NOTE: apparently this is all you need?
}

float_t PhysicsEngine::getGravityStrength()
{
	return _gravityStrength;
}

RegisteredPhysicsObject* PhysicsEngine::registerPhysicsObject(float_t mass, glm::vec3 origin, glm::quat rotation, btCollisionShape* shape)
{
	// @NOTE: this is required to be here (as well as the .reserve() on init)
	//        bc if the capacity is overcome, then a new array with a larger
	//        capacity is allocated, and then the pointer to the part in the
	//        vector is lost to garbage memory that got deallocated.  -Timo 2022/10/24
	if (_physicsObjects.size() >= PHYSICS_OBJECTS_MAX_CAPACITY)
	{
		std::cerr << "[REGISTER PHYSICS OBJECT]" << std::endl
			<< "ERROR: trying to register physics object when capacity is at maximum." << std::endl
			<< "       Current capacity: " << _physicsObjects.size() << std::endl
			<< "       Maximum capacity: " << PHYSICS_OBJECTS_MAX_CAPACITY << std::endl;
		return nullptr;
	}

	glm::mat4 glmTrans = glm::translate(glm::mat4(1.0f), origin) * glm::toMat4(rotation);

	btTransform trans;
	trans.setFromOpenGLMatrix(glm::value_ptr(glmTrans));

	RegisteredPhysicsObject rpo = {
		.body = createRigidBody(mass, trans, shape),
		.prevTransform = trans,    // Set it to this so there's a basis to do the interpolation from
		.interpolatedTransform = glmTrans,
	};
	_physicsObjects.push_back(rpo);

	recreateDebugDrawBuffer();

	return &_physicsObjects.back();
}

void PhysicsEngine::unregisterPhysicsObject(RegisteredPhysicsObject* objRegistration)
{
	std::erase_if(_physicsObjects,
		[&](RegisteredPhysicsObject& x) {
			bool deleteFlag = (&x == objRegistration);
			if (deleteFlag)
				_dynamicsWorld->removeRigidBody(x.body);
			return deleteFlag;
		}
	);

	recreateDebugDrawBuffer();
}

void PhysicsEngine::renderDebugDraw(VkCommandBuffer cmd, const VkDescriptorSet& globalDescriptor)
{
	Material& debugDrawMaterial = *_engine->getMaterial("debugPhysicsObjectMaterial");
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugDrawMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugDrawMaterial.pipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugDrawMaterial.pipelineLayout, 1, 1, &_transformsDescriptor, 0, nullptr);
	ColorPushConstBlock pc = {
		.color = glm::vec4(0, 1, 0, 1),
	};	
	vkCmdPushConstants(cmd, debugDrawMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ColorPushConstBlock), &pc);

	const VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(cmd, 0, 1, &_vertexBuffer._buffer, offsets);
	vkCmdDraw(cmd, _vertexCount, 1, 0, 0);
}

std::vector<VkVertexInputAttributeDescription> PhysicsEngine::getVertexAttributeDescriptions()
{
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkVertexInputAttributeDescription posAttribute = {
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(DebugDrawVertex, pos),
	};
	VkVertexInputAttributeDescription transformIndexAttribute = {
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32_SINT,
		.offset = offsetof(DebugDrawVertex, physObjIndex),
	};

	attributes.push_back(posAttribute);
	attributes.push_back(transformIndexAttribute);
	return attributes;
}

std::vector<VkVertexInputBindingDescription> PhysicsEngine::getVertexBindingDescriptions()
{
	std::vector<VkVertexInputBindingDescription> bindings;

	VkVertexInputBindingDescription mainBinding = {
		.binding = 0,
		.stride = sizeof(DebugDrawVertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	bindings.push_back(mainBinding);
	return bindings;
}

void PhysicsEngine::calculateInterpolatedTransform(RegisteredPhysicsObject& obj, const float_t& physicsAlpha)
{
	btTransform currentTransform = obj.body->getWorldTransform();
	btTransform interpolatedTransform(
		obj.prevTransform.getRotation().slerp(currentTransform.getRotation(), physicsAlpha),    // NLerp nor lerp are available for btQuaternions smh
		obj.prevTransform.getOrigin().lerp(currentTransform.getOrigin(), physicsAlpha)
		    + btVector3(obj.transformOffset.x, obj.transformOffset.y, obj.transformOffset.z)
	);
	interpolatedTransform.getOpenGLMatrix(glm::value_ptr(obj.interpolatedTransform));  // Apply to the interpolatedTransform matrix!
	obj.prevTransform = currentTransform;
}

btRigidBody* PhysicsEngine::createRigidBody(float_t mass, const btTransform& startTransform, btCollisionShape* shape, const btVector4& color)
{
	btAssert((!shape || shape->getShapeType() != INVALID_SHAPE_PROXYTYPE));

	//rigidbody is dynamic if and only if mass is non zero, otherwise static
	bool isDynamic = (mass != 0.f);

	btVector3 localInertia(0, 0, 0);
	if (isDynamic)
		shape->calculateLocalInertia(mass, localInertia);    // STFU, C6011

		//using motionstate is recommended, it provides interpolation capabilities, and only synchronizes 'active' objects

#define USE_MOTIONSTATE 1
#ifdef USE_MOTIONSTATE
	btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
	btRigidBody::btRigidBodyConstructionInfo cInfo(mass, myMotionState, shape, localInertia);
	btRigidBody* body = new btRigidBody(cInfo);
	//body->setContactProcessingThreshold(m_defaultContactProcessingThreshold);    // @TODO: what does this do?
#else
	btRigidBody* body = new btRigidBody(mass, 0, shape, localInertia);
	body->setWorldTransform(startTransform);
#endif

	body->setUserIndex(-1);
	_dynamicsWorld->addRigidBody(body);
	return body;
}

void PhysicsEngine::appendDebugShapeVertices(btBoxShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList)
{
	DebugDrawVertex v = {
		.physObjIndex = static_cast<int32_t>(physObjIndex),
	};
	auto temp = shape->getHalfExtentsWithMargin();
	glm::vec3 he = glm::vec3(temp.getX(), temp.getY(), temp.getZ());  // Short for Half Extents
	
	v.pos = glm::vec3(-1, 1, 1) * he;
	vertexList.push_back(v);
	v.pos = glm::vec3(1, 1, 1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(1, 1, -1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(-1, 1, -1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(-1, 1, 1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(-1, -1, 1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(1, -1, 1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(1, -1, -1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(-1, -1, -1) * he;
	vertexList.push_back(v);

	vertexList.push_back(v);
	v.pos = glm::vec3(-1, -1, 1) * he;
	vertexList.push_back(v);
	
	v.pos = glm::vec3(-1, -1, -1) * he;
	vertexList.push_back(v);
	v.pos = glm::vec3(-1, 1, -1) * he;
	vertexList.push_back(v);
	
	v.pos = glm::vec3(1, -1, -1) * he;
	vertexList.push_back(v);
	v.pos = glm::vec3(1, 1, -1) * he;
	vertexList.push_back(v);

	v.pos = glm::vec3(1, -1, 1) * he;
	vertexList.push_back(v);
	v.pos = glm::vec3(1, 1, 1) * he;
	vertexList.push_back(v);
}

void PhysicsEngine::appendDebugShapeVertices(btSphereShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList)
{
	DebugDrawVertex v = {
		.physObjIndex = static_cast<int32_t>(physObjIndex),
	};
	float_t radius = shape->getRadius();

	constexpr int32_t circleSlices = 16;
	const float_t circleStride = glm::radians(360.0f) / (float_t)circleSlices;
	float_t ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(glm::sin(ca),            0, glm::cos(ca)) * radius;
		vertexList.push_back(v);
		if (i > 0 && i < circleSlices)
			vertexList.push_back(v);
	}
	ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(           0, glm::sin(ca), glm::cos(ca)) * radius;
		vertexList.push_back(v);
		if (i > 0 && i < circleSlices)
			vertexList.push_back(v);
	}
	ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(glm::sin(ca), glm::cos(ca),            0) * radius;
		vertexList.push_back(v);
		if (i > 0 && i < circleSlices)
			vertexList.push_back(v);
	}
}

void PhysicsEngine::appendDebugShapeVertices(btCylinderShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList)
{
	DebugDrawVertex v = {
		.physObjIndex = static_cast<int32_t>(physObjIndex),
	};
	auto temp = shape->getHalfExtentsWithMargin();
	glm::vec3 he = glm::vec3(temp.getX(), temp.getY(), temp.getZ());  // Short for Half Extents
	
	constexpr int32_t circleSlices = 16;
	const float_t circleStride = glm::radians(360.0f) / (float_t)circleSlices;
	float_t ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(glm::sin(ca) * he.x,  he.y, glm::cos(ca) * he.z);
		vertexList.push_back(v);
		if (i > 0 && i < circleSlices)
			vertexList.push_back(v);
	}
	ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(glm::sin(ca) * he.x, -he.y, glm::cos(ca) * he.z);
		vertexList.push_back(v);
		if (i > 0 && i < circleSlices)
			vertexList.push_back(v);
	}
	
	v.pos = glm::vec3(he.x, he.y, 0);
	vertexList.push_back(v);
	v.pos = glm::vec3(he.x, -he.y, 0);
	vertexList.push_back(v);

	v.pos = glm::vec3(-he.x, he.y, 0);
	vertexList.push_back(v);
	v.pos = glm::vec3(-he.x, -he.y, 0);
	vertexList.push_back(v);

	v.pos = glm::vec3(0, he.y, he.z);
	vertexList.push_back(v);
	v.pos = glm::vec3(0, -he.y, he.z);
	vertexList.push_back(v);

	v.pos = glm::vec3(0, he.y, -he.z);
	vertexList.push_back(v);
	v.pos = glm::vec3(0, -he.y, -he.z);
	vertexList.push_back(v);
}

void PhysicsEngine::appendDebugShapeVertices(btCapsuleShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList)
{
	DebugDrawVertex v = {
		.physObjIndex = static_cast<int32_t>(physObjIndex),
	};
	float_t radius = shape->getRadius();
	float_t halfHeight = shape->getHalfHeight();
	float_t drawHeight = glm::max(0.0f, halfHeight - radius);
	
	constexpr int32_t circleSlices = 16;
	const float_t circleStride = glm::radians(360.0f) / (float_t)circleSlices;
	float_t ca = 0.0f;      // "The code is its own documentation" ... to take the red pill, keep reading.      ---->                                                          `ca` means 'current angle'. Now ask yourself, was the code self-documenting?  -Dmitri
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(glm::sin(ca) * radius,  drawHeight, glm::cos(ca) * radius);
		vertexList.push_back(v);
		if (i > 0 && i < circleSlices)
			vertexList.push_back(v);
	}
	ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(glm::sin(ca) * radius, -drawHeight, glm::cos(ca) * radius);
		vertexList.push_back(v);
		if (i > 0 && i < circleSlices)
			vertexList.push_back(v);
	}

	ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(0, glm::sin(ca), glm::cos(ca)) * radius;
		if (i == circleSlices / 2)
		{
			v.pos.y += drawHeight;
			vertexList.push_back(v);
			vertexList.push_back(v);
			v.pos.y *= -1.0f;
			vertexList.push_back(v);
			vertexList.push_back(v);
		}
		else if (i < circleSlices / 2)
		{
			v.pos.y += drawHeight;
			vertexList.push_back(v);
			if (i > 0 && i < circleSlices)
				vertexList.push_back(v);
		}
		else if (i == circleSlices)
		{
			v.pos.y -= drawHeight;
			vertexList.push_back(v);
			vertexList.push_back(v);
			v.pos.y *= -1.0f;
			vertexList.push_back(v);
		}
		else
		{
			v.pos.y -= drawHeight;
			vertexList.push_back(v);
			if (i > 0 && i < circleSlices)
				vertexList.push_back(v);
		}
	}
	ca = 0.0f;
	for (int32_t i = 0; i <= circleSlices; i++, ca += circleStride)
	{
		v.pos = glm::vec3(glm::cos(ca), glm::sin(ca), 0) * radius;
		if (i == circleSlices / 2)
		{
			v.pos.y += drawHeight;
			vertexList.push_back(v);
			vertexList.push_back(v);
			v.pos.y *= -1.0f;
			vertexList.push_back(v);
			vertexList.push_back(v);
		}
		else if (i < circleSlices / 2)
		{
			v.pos.y += drawHeight;
			vertexList.push_back(v);
			if (i > 0 && i < circleSlices)
				vertexList.push_back(v);
		}
		else if (i == circleSlices)
		{
			v.pos.y -= drawHeight;
			vertexList.push_back(v);
			vertexList.push_back(v);
			v.pos.y *= -1.0f;
			vertexList.push_back(v);
		}
		else
		{
			v.pos.y -= drawHeight;
			vertexList.push_back(v);
			if (i > 0 && i < circleSlices)
				vertexList.push_back(v);
		}
	}
}

void PhysicsEngine::recreateDebugDrawBuffer()
{
	//
	// Assemble vertices with the correct shape sizing
	//
	std::vector<DebugDrawVertex> vertexList;
	for (size_t i = 0; i < _physicsObjects.size(); i++)
	{
		btCollisionShape* shape = _physicsObjects[i].body->getCollisionShape();

		switch (shape->getShapeType())
		{
		case BOX_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btBoxShape*)shape, i, vertexList);
			break;
		case SPHERE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btSphereShape*)shape, i, vertexList);
			break;
		case CYLINDER_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCylinderShape*)shape, i, vertexList);
			break;
		case CAPSULE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCapsuleShape*)shape, i, vertexList);
			break;
		default:
			std::cerr << "[CREATING PHYSICS ENGINE DEBUG DRAW BUFFER]" << std::endl
				<< "ERROR: shape type currently not supported: " << shape->getShapeType() << std::endl;
			break;
		}
	}

	//
	// Construct gpu visible buffer with all of these vertices
	//
	if (_vertexBufferCreated)
	{
		// Delete created buffer before recreation
		vmaDestroyBuffer(_engine->_allocator, _vertexBuffer._buffer, _vertexBuffer._allocation);
		_vertexBufferCreated = false;
	}

	size_t vertexBufferSize = vertexList.size() * sizeof(DebugDrawVertex);
	_vertexBuffer =
		_engine->createBuffer(
			vertexBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU
		);
	_vertexCount = vertexList.size();
	_vertexBufferCreated = true;

	void* data;
	vmaMapMemory(_engine->_allocator, _vertexBuffer._allocation, &data);
	memcpy(data, &vertexList[0], vertexBufferSize);
	vmaUnmapMemory(_engine->_allocator, _vertexBuffer._allocation);
}

namespace physutil
{
	float_t smoothStep(float_t edge0, float_t edge1, float_t t)
	{
		t = std::clamp((t - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3 - 2 * t);
	}

	float_t moveTowards(float_t current, float_t target, float_t maxDistanceDelta)
	{
		float_t delta = target - current;
		return (maxDistanceDelta >= std::abs(delta)) ? target : (current + std::copysignf(1.0f, delta) * maxDistanceDelta);
	}

	glm::i64 moveTowards(glm::i64 current, glm::i64 target, glm::i64 maxDistanceDelta)
	{
		glm::i64 delta = target - current;
		return (maxDistanceDelta >= glm::abs(delta)) ? target : (current + glm::sign(delta) * maxDistanceDelta);
	}

	float_t moveTowardsAngle(float_t currentAngle, float_t targetAngle, float_t maxTurnDelta)
	{
		float_t result;
		float_t diff = targetAngle - currentAngle;
		if (diff < -180.0f)
		{
			// Move upwards past 360
			targetAngle += 360.0f;
			result = moveTowards(currentAngle, targetAngle, maxTurnDelta);
			if (result >= 360.0f)
			{
				result -= 360.0f;
			}
		}
		else if (diff > 180.0f)
		{
			// Move downwards past 0
			targetAngle -= 360.0f;
			result = moveTowards(currentAngle, targetAngle, maxTurnDelta);
			if (result < 0.0f)
			{
				result += 360.0f;
			}
		}
		else
		{
			// Straight move
			result = moveTowards(currentAngle, targetAngle, maxTurnDelta);
		}

		return result;
	}

	glm::vec2 moveTowardsVec2(glm::vec2 current, glm::vec2 target, float_t maxDistanceDelta)
	{
		float_t delta = glm::length(target - current);
		glm::vec2 mvtDeltaNormalized = glm::normalize(target - current);
		return (maxDistanceDelta >= std::abs(delta)) ? target : (current + mvtDeltaNormalized * maxDistanceDelta);
	}

	glm::vec3 moveTowardsVec3(glm::vec3 current, glm::vec3 target, float_t maxDistanceDelta)
	{
		float_t delta = glm::length(target - current);
		glm::vec3 mvtDeltaNormalized = glm::normalize(target - current);
		return (maxDistanceDelta >= std::abs(delta)) ? target : (current + mvtDeltaNormalized * maxDistanceDelta);
	}

	glm::vec3 clampVector(glm::vec3 vector, float_t min, float_t max)
	{
		float_t magnitude = glm::length(vector);

		assert(std::abs(magnitude) > 0.00001f);

		return glm::normalize(vector) * std::clamp(magnitude, min, max);
	}

    btVector3 toVec3(const glm::vec3& vector)
	{
		return btVector3(vector.x, vector.y, vector.z);
	}

    glm::vec3 toVec3(const btVector3& vector)
	{
		return glm::vec3(vector.x(), vector.y(), vector.z());
	}

	glm::vec3 getPosition(const glm::mat4& transform)
	{
		return glm::vec3(transform[3]);
	}
}