#include "PhysicsEngine.h"

#include <iostream>
#include "VulkanEngine.h"
#include "Entity.h"
#include "WindManager.h"
#include "VkInitializers.h"


void RegisteredPhysicsObject::reportMoved(const glm::mat4& newTrans, bool resetVelocity)
{
	body->setWorldTransform(
        physutil::toTransform(newTrans)
    );
	currentTransform      = body->getWorldTransform();
	prevTransform         = body->getWorldTransform();
	interpolatedTransform = newTrans;

	if (resetVelocity)
    	body->setLinearVelocity(btVector3(0, 0, 0));
}

void RegisteredGhostObject::reportMoved(const glm::mat4& newTrans)
{
	ghost->setWorldTransform(
		physutil::toTransform(newTrans)
	);
}

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
	_gravityDirection = _dynamicsWorld->getGravity().normalized();

	//
	// Create 
	//
	_transformsBuffer =
		_engine->createBuffer(
			sizeof(GPUObjectData) * _physicsObjectPool.size(),  // Pray that GPUObjectData doesn't change!
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
		.range = sizeof(GPUObjectData) * _physicsObjectPool.size(),
	};
	VkWriteDescriptorSet transformsWrite = vkinit::writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _transformsDescriptor, &transformsBufferInfo, 0);
	vkUpdateDescriptorSets(_engine->_device, 1, &transformsWrite, 0, nullptr);

	//
	// Reserve extra debug draw vertices
	//
	_oneFrameVertexList.reserve(_oneFrameVertexListAllocation);
}

void PhysicsEngine::update(float_t deltaTime, std::vector<Entity*>* entities)    // https://gafferongames.com/post/fix_your_timestep/
{
	constexpr float_t physicsDeltaTime = 0.04f;    // 25fps
	_accumulatedTimeForPhysics += deltaTime;

	//
	// Step thru the simulation
	//
	for (; _accumulatedTimeForPhysics >= physicsDeltaTime; _accumulatedTimeForPhysics -= physicsDeltaTime)
	{
		_oneFrameVertexList.clear();  // Clear the vertex list every time physicsupdate() will get called

		for (auto it = entities->begin(); it != entities->end(); it++)    // @IMPROVEMENT: this could be multithreaded if we're just smart by how we do the physicsupdates
		{
			Entity* ent = *it;
			if (ent->_enablePhysicsUpdate)
				ent->physicsUpdate(physicsDeltaTime);
		}

		// @IMPROVEMENT: look into how bullet's step simulation does with interpolation and how we can rely on that instead of the system here... it could be good especially if it's deterministic seeming. The only problem really seems to be what we'd put in for the deltaTime of physicsUpdate() for all the objects
		_dynamicsWorld->stepSimulation(physicsDeltaTime, 0);    // @NOTE: only want the step simulation to happen once at a time

		// @IMPROVEMENT: @TODO: oh man, this should definitely be multithreaded. However, taskflow doesn't seem like the best choice.  @TOOD: look into the c++11 multithreaded for loop
		// Update transforms
		for (size_t poolIndex : _physicsObjectsIndices)
		{
			auto& rpo = _physicsObjectPool[poolIndex];
			rpo.prevTransform = rpo.currentTransform;
			rpo.currentTransform = rpo.body->getWorldTransform();
		}
	}

	//
	// Check contact manifolds
	// @NOTE: using a system similar to Unity's messaging OnCollisionStay() (just oncollisionstay bc that's the only one I use personally)
	//
	size_t numManifolds = (size_t)_dispatcher->getNumManifolds();
	for (size_t i = 0; i < numManifolds; i++)
	{
		btPersistentManifold* manifold = _dispatcher->getManifoldByIndexInternal((int32_t)i);  // @NOTE: a manifold is a set of contacts that came from a collision. Ideally it should store normal hit info too.
		if (manifold->getNumContacts() <= 0)
			continue;

		//
		// @HACK: the `am_i_b` part... holy buttcrack, that's really dumb.
		//
		// @TODO: Well, in the future it might be really good to also give the other
		//        `RegisteredPhysicsObject*` pointer so that tags and whatnot can get
		//        compared to each other.  -Timo
		//
		RegisteredPhysicsObject* obj0 = _rigidBodyToPhysicsObjectMap[(void*)manifold->getBody0()];
		RegisteredPhysicsObject* obj1 = _rigidBodyToPhysicsObjectMap[(void*)manifold->getBody1()];
		if (obj0 && obj0->onCollisionStayCallback)
			(*obj0->onCollisionStayCallback)(manifold, false);
		if (obj1 && obj1->onCollisionStayCallback)
			(*obj1->onCollisionStayCallback)(manifold, true);
	}

	//
	// Interpolate the positions of all physics objects
	//
	const float_t physicsAlpha = _accumulatedTimeForPhysics / physicsDeltaTime;
#ifdef _DEVELOP
	// @TODO: Make a playmode flag
	//if (!playMode)
	//	physicsAlpha = 1.0f;
#endif

	// Calculate all the interpolated transforms
	for (size_t poolIndex : _physicsObjectsIndices)    // @IMPROVEMENT: @TODO: oh man, this should definitely be multithreaded. However, taskflow doesn't seem like the best choice.  @TOOD: look into the c++11 multithreaded for loop
		calculateInterpolatedTransform(_physicsObjectPool[poolIndex], physicsAlpha);

#ifdef _DEVELOP
	// Fill in the transform buffer with the original transforms (For debug view)
	void* objectData;
	vmaMapMemory(_engine->_allocator, _transformsBuffer._allocation, &objectData);   // And what happens if you overwrite the memory during a frame? Well, idk, but it shouldn't be too bad for debugging purposes I think  -Timo 2022/10/24
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;
	for (size_t poolIndex : _physicsObjectsIndices)
	{
		_physicsObjectPool[poolIndex].body->getWorldTransform().getOpenGLMatrix(glm::value_ptr(objectSSBO->modelMatrix));
		objectSSBO++;  // Extra Dmitri in this one
	}
	for (size_t poolIndex : _ghostObjectsIndices)
	{
		_ghostObjectPool[poolIndex].ghost->getWorldTransform().getOpenGLMatrix(glm::value_ptr(objectSSBO->modelMatrix));
		objectSSBO++;
	}
	vmaUnmapMemory(_engine->_allocator, _transformsBuffer._allocation);
#endif

	// Let windmgr possibly write debug collision
	windmgr::debugRenderCollisionData(this);

	// Load the debug draw lines emplaced from inside the physicsUpdate()'s
	loadOneFrameDebugDrawLines();
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

btVector3 PhysicsEngine::getGravityDirection()
{
	return _gravityDirection;
}

btVector3 PhysicsEngine::getGravity()
{
	return _dynamicsWorld->getGravity();
}

RegisteredPhysicsObject* PhysicsEngine::registerPhysicsObject(float_t mass, glm::vec3 origin, glm::quat rotation, btCollisionShape* shape, void* guid)
{
	// @NOTE: I'm putting in a new system where instead of a reserved vector,
	//        there's now a pool where you can register physics objects from
	//        and indices of the objects are kept track of... so it's harder
	//        to create the idea above (allocating new pool as soon as capacity
	//        is met)  -Timo 2022/11/06
	if (_physicsObjectsIndices.size() >= PHYSICS_OBJECTS_MAX_CAPACITY)
	{
		std::cerr << "[REGISTER PHYSICS OBJECT]" << std::endl
			<< "ERROR: trying to register physics object when capacity is at maximum." << std::endl
			<< "       Current capacity: " << _physicsObjectsIndices.size() << std::endl
			<< "       Maximum capacity: " << _physicsObjectPool.size() << std::endl;
		return nullptr;
	}

	glm::mat4 glmTrans = glm::translate(glm::mat4(1.0f), origin) * glm::toMat4(rotation);

	btTransform trans = physutil::toTransform(glmTrans);
	RegisteredPhysicsObject rpo = {
		.body                  = createRigidBody(mass, trans, shape, guid),
		.currentTransform      = trans,    // Set it to this so there's a basis to do the interpolation from
		.prevTransform         = trans,    // Set it to this so there's a basis to do the interpolation from
		.interpolatedTransform = glmTrans,
	};

	// Find next open spot in pool
	// @NOTE: I'm sure there are many things to improve the speed
	//        of this... but I don't think we'll be adding physics objects
	//        very often except for just loading them in initially. But,
	//        if you do happen to need to, the idea I had was to keep a cursor index
	//        of the next open spot in the pool, and then when using up an open spot
	//        just incrementing the cursor once, so when you try to register another
	//        one, you just increment the cursor forwards, wrapping, until you find
	//        another open slot, then increment to the next one, then repeat.
	//        Of course, if the next one you incremented to happened to be an open spot
	//        already, you dno't have to do the keep incrementing, wrapping, thing next
	//        time you try to register a new physics object in the pool.  -Timo 2022/11/06
	size_t registerIndex = 0;
	for (size_t i = 0; i < _physicsObjectsIsRegistered.size(); i++)
	{
		if (!_physicsObjectsIsRegistered[i])
		{
			registerIndex = i;
			break;
		}
	}

	// Register object
	_physicsObjectPool[registerIndex] = rpo;
	_physicsObjectsIsRegistered[registerIndex] = true;
	_physicsObjectsIndices.push_back(registerIndex);
	_rigidBodyToPhysicsObjectMap[(void*)rpo.body] = &_physicsObjectPool[registerIndex];

	_recreateDebugDrawBuffer = true;

	return &_physicsObjectPool[registerIndex];
}

void PhysicsEngine::unregisterPhysicsObject(RegisteredPhysicsObject* objRegistration)
{
	size_t indicesIndex = 0;
	for (size_t poolIndex : _physicsObjectsIndices)
	{
		auto& rpo = _physicsObjectPool[poolIndex];
		if (&rpo == objRegistration)
		{
			// Unregister object
			_dynamicsWorld->removeRigidBody(rpo.body);
			_rigidBodyToPhysicsObjectMap.erase((void*)rpo.body);
			_physicsObjectsIsRegistered[poolIndex] = false;
			_physicsObjectsIndices.erase(_physicsObjectsIndices.begin() + indicesIndex);

			_recreateDebugDrawBuffer = true;

			return;
		}

		indicesIndex++;
	}

	std::cerr << "[UNREGISTER PHYSICS OBJECT]" << std::endl
		<< "ERROR: physics object " << objRegistration << " was not found. Nothing unregistered." << std::endl;
}

RegisteredPhysicsObject* PhysicsEngine::getPhysicsObjectFromVoidPtr(void* ptr)
{
	return _rigidBodyToPhysicsObjectMap[ptr];
}

RegisteredGhostObject* PhysicsEngine::registerGhostObject(const glm::vec3& origin, const glm::quat& rotation, btCollisionShape* shape, void* guid)
{
	// @NOTE: this whole system just copies the one for the physics objects
	if (_ghostObjectsIndices.size() >= PHYSICS_OBJECTS_MAX_CAPACITY)
	{
		std::cerr << "[REGISTER GHOST OBJECT]" << std::endl
			<< "ERROR: trying to register ghost object when capacity is at maximum." << std::endl
			<< "       Current capacity: " << _ghostObjectsIndices.size() << std::endl
			<< "       Maximum capacity: " << _ghostObjectPool.size() << std::endl;
		return nullptr;
	}

	glm::mat4 glmTrans = glm::translate(glm::mat4(1.0f), origin) * glm::toMat4(rotation);

	btTransform trans = physutil::toTransform(glmTrans);
	RegisteredGhostObject rgo = {
		.ghost = createGhostObject(trans, shape, guid),
	};

	// Find next open spot in pool
	// @COPYPASTA
	size_t registerIndex = 0;
	for (size_t i = 0; i < _ghostObjectsIsRegistered.size(); i++)
	{
		if (!_ghostObjectsIsRegistered[i])
		{
			registerIndex = i;
			break;
		}
	}

	// Register object
	_ghostObjectPool[registerIndex] = rgo;
	_ghostObjectsIsRegistered[registerIndex] = true;
	_ghostObjectsIndices.push_back(registerIndex);
	_ghostObjectToRegisteredGhostObjectMap[(void*)rgo.ghost] = &_ghostObjectPool[registerIndex];

	_recreateDebugDrawBuffer = true;

	return &_ghostObjectPool[registerIndex];
}

void PhysicsEngine::unregisterGhostObject(RegisteredGhostObject* objRegistration)
{
	size_t indicesIndex = 0;
	for (size_t poolIndex : _ghostObjectsIndices)
	{
		auto& rgo = _ghostObjectPool[poolIndex];
		if (&rgo == objRegistration)
		{
			// Unregister object
			_dynamicsWorld->removeCollisionObject(rgo.ghost);
			_ghostObjectToRegisteredGhostObjectMap.erase((void*)rgo.ghost);
			_physicsObjectsIsRegistered[poolIndex] = false;
			_ghostObjectsIndices.erase(_ghostObjectsIndices.begin() + indicesIndex);

			_recreateDebugDrawBuffer = true;

			return;
		}

		indicesIndex++;
	}

	std::cerr << "[UNREGISTER GHOST OBJECT]" << std::endl
		<< "ERROR: ghost object " << objRegistration << " was not found. Nothing unregistered." << std::endl;
}

void PhysicsEngine::lazyRecreateDebugDrawBuffer()
{
	if (!_recreateDebugDrawBuffer)
		return;

	recreateDebugDrawBuffer();
}

void PhysicsEngine::renderDebugDraw(VkCommandBuffer cmd, const VkDescriptorSet& globalDescriptor)
{
	Material& debugDrawMaterial = *_engine->getMaterial("debugPhysicsObjectMaterial");
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugDrawMaterial.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugDrawMaterial.pipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugDrawMaterial.pipelineLayout, 1, 1, &_transformsDescriptor, 0, nullptr);

	const VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(cmd, 0, 1, &_vertexBuffer._buffer, offsets);
	vkCmdDraw(cmd, static_cast<uint32_t>(_vertexCount + _oneFrameVertexList.size()), 1, 0, 0);
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
	VkVertexInputAttributeDescription colorAttribute = {
		.location = 2,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(DebugDrawVertex, color),
	};

	attributes.push_back(posAttribute);
	attributes.push_back(transformIndexAttribute);
	attributes.push_back(colorAttribute);
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

btCollisionWorld::ClosestRayResultCallback PhysicsEngine::raycast(const btVector3& from, const btVector3& to, int32_t filterGroups, int32_t mask, uint32_t flags)
{
	btCollisionWorld::ClosestRayResultCallback closestResults(from, to);
	closestResults.m_flags |= flags;
	closestResults.m_collisionFilterGroup = filterGroups;
	closestResults.m_collisionFilterMask = mask;
	_dynamicsWorld->rayTest(from, to, closestResults);
	return closestResults;
}

btCollisionWorld::ClosestConvexResultCallback PhysicsEngine::boxcast(const btTransform& from, const btTransform& to, const glm::vec3& halfExtents, int32_t filterGroups, int32_t mask)
{
	btCollisionWorld::ClosestConvexResultCallback resultCallback(from.getOrigin(), to.getOrigin());
	resultCallback.m_collisionFilterGroup = filterGroups;
	resultCallback.m_collisionFilterMask = mask;
	btBoxShape shape(physutil::toVec3(halfExtents));
	_dynamicsWorld->convexSweepTest(&shape, from, to, resultCallback);
	return resultCallback;
}

void PhysicsEngine::calculateInterpolatedTransform(RegisteredPhysicsObject& obj, const float_t& physicsAlpha)
{
	btTransform interpolatedTransform(
		obj.prevTransform.getRotation().slerp(obj.currentTransform.getRotation(), physicsAlpha),    // NLerp nor lerp are available for btQuaternions smh
		obj.prevTransform.getOrigin().lerp(obj.currentTransform.getOrigin(), physicsAlpha)
		    + btVector3(obj.transformOffset.x, obj.transformOffset.y, obj.transformOffset.z)
	);
	interpolatedTransform.getOpenGLMatrix(glm::value_ptr(obj.interpolatedTransform));  // Apply to the interpolatedTransform matrix!
}

btRigidBody* PhysicsEngine::createRigidBody(float_t mass, const btTransform& startTransform, btCollisionShape* shape, void* guid, const btVector4& color)
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

	body->setUserPointer(guid);
	body->setUserIndex(-1);
	_dynamicsWorld->addRigidBody(body);
	return body;
}

/*
void motorPreTickCallback (btDynamicsWorld *world, btScalar timeStep)
{
  	for(int i = 0; i < ghostObject->getNumOverlappingObjects(); i++)
 	{
        if (btRigidBody* body = dynamic_cast<btRigidBody*>(ghostObject->getOverlappingObject(i)))
		{
			
		}
        // do whatever you want to do with these pairs of colliding objects
    }
}
*/

btPairCachingGhostObject* PhysicsEngine::createGhostObject(const btTransform& startTransform, btCollisionShape* shape, void* guid)
{
	btPairCachingGhostObject* ghost = new btPairCachingGhostObject();
	ghost->setCollisionShape(shape);
	// ghost->setCollisionFlags(btCollisionObject::CF_NO_CONTACT_RESPONSE);
	ghost->setWorldTransform(startTransform);
	ghost->setUserPointer(guid);
	ghost->setUserIndex(-1);
	_dynamicsWorld->addCollisionObject(ghost, btBroadphaseProxy::SensorTrigger, btBroadphaseProxy::AllFilter & ~btBroadphaseProxy::SensorTrigger);

	static bool did = false;
	if (!did)
	{
		_dynamicsWorld->getBroadphase()->getOverlappingPairCache()->setInternalGhostPairCallback(new btGhostPairCallback());
		did = true;
	}

	return ghost;
}

void PhysicsEngine::appendDebugShapeVertices(btBoxShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList)
{
	DebugDrawVertex v = {
		.physObjIndex = static_cast<int32_t>(physObjIndex),
		.color = glm::vec3(0, 1, 0),
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
		.color = glm::vec3(0, 1, 0),
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
		.color = glm::vec3(0, 1, 0),
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
		.color = glm::vec3(0, 1, 0),
	};
	float_t radius = shape->getRadius();
	float_t halfHeight = shape->getHalfHeight() + shape->getMargin();
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

void PhysicsEngine::appendDebugShapeVertices(btCompoundShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList)
{
	size_t numChildShapes       = shape->getNumChildShapes();
	btCompoundShapeChild* child = shape->getChildList();
	for (size_t i = 0; i < numChildShapes; i++)
	{
		glm::mat4 trans(1.0f);
		child->m_transform.getOpenGLMatrix(glm::value_ptr(trans));
		std::vector<DebugDrawVertex> tempVL;

		// @COPYPASTA
		btCollisionShape* shape = child->m_childShape;

		switch (child->m_childShapeType)
		{
		case BOX_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btBoxShape*)shape, physObjIndex, tempVL);
			break;
		case SPHERE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btSphereShape*)shape, physObjIndex, tempVL);
			break;
		case CYLINDER_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCylinderShape*)shape, physObjIndex, tempVL);
			break;
		case CAPSULE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCapsuleShape*)shape, physObjIndex, tempVL);
			break;
		/*case COMPOUND_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCompoundShape*)shape, physObjIndex, tempVL);
			break;*/
		default:
			std::cerr << "[CREATING PHYSICS ENGINE DEBUG DRAW BUFFER]" << std::endl
				<< "ERROR: shape type currently not supported: " << shape->getShapeType() << std::endl;
			break;
		}

		// Transform the temporary vertex list into the correct transform
		for (auto& ddv : tempVL)
		{
			ddv.pos = trans * glm::vec4(ddv.pos, 1.0f);
			vertexList.push_back(ddv);
		}

		child++;
	}
}

void PhysicsEngine::recreateDebugDrawBuffer()
{
	vkDeviceWaitIdle(_engine->_device);

	//
	// Assemble vertices with the correct shape sizing
	//
	std::vector<DebugDrawVertex> vertexList;
	size_t ssboIndex = 0;  // @NOTE: since the ssbo gets added in in a linear fashion, this also needs to increment in a linear fashion instead of relying on the poolIndex  -Timo
	for (size_t poolIndex : _physicsObjectsIndices)
	{
		btCollisionShape* shape = _physicsObjectPool[poolIndex].body->getCollisionShape();

		switch (shape->getShapeType())
		{
		case BOX_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btBoxShape*)shape, ssboIndex, vertexList);
			break;
		case SPHERE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btSphereShape*)shape, ssboIndex, vertexList);
			break;
		case CYLINDER_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCylinderShape*)shape, ssboIndex, vertexList);
			break;
		case CAPSULE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCapsuleShape*)shape, ssboIndex, vertexList);
			break;
		case COMPOUND_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCompoundShape*)shape, ssboIndex, vertexList);
			break;
		default:
			std::cerr << "[CREATING PHYSICS ENGINE DEBUG DRAW BUFFER]" << std::endl
				<< "ERROR: shape type currently not supported: " << shape->getShapeType() << std::endl;
			break;
		}
		ssboIndex++;
	}

	for (size_t poolIndex : _ghostObjectsIndices)  // @COPYPASTA
	{
		btCollisionShape* shape = _ghostObjectPool[poolIndex].ghost->getCollisionShape();

		switch (shape->getShapeType())
		{
		case BOX_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btBoxShape*)shape, ssboIndex, vertexList);
			break;
		case SPHERE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btSphereShape*)shape, ssboIndex, vertexList);
			break;
		case CYLINDER_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCylinderShape*)shape, ssboIndex, vertexList);
			break;
		case CAPSULE_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCapsuleShape*)shape, ssboIndex, vertexList);
			break;
		case COMPOUND_SHAPE_PROXYTYPE:
			appendDebugShapeVertices((btCompoundShape*)shape, ssboIndex, vertexList);
			break;
		default:
			std::cerr << "[CREATING PHYSICS ENGINE DEBUG DRAW BUFFER]" << std::endl
				<< "ERROR: shape type currently not supported: " << shape->getShapeType() << std::endl;
			break;
		}
		ssboIndex++;
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

	size_t vertexBufferSize = (vertexList.size() + _oneFrameVertexListAllocation) * sizeof(DebugDrawVertex);
	_vertexBuffer =
		_engine->createBuffer(
			vertexBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU
		);
	_vertexCount = vertexList.size();
	_vertexBufferCreated = true;

	if (_vertexCount > 0)
	{
		void* data;
		vmaMapMemory(_engine->_allocator, _vertexBuffer._allocation, &data);
		memcpy(data, &vertexList[0], _vertexCount * sizeof(DebugDrawVertex));
		vmaUnmapMemory(_engine->_allocator, _vertexBuffer._allocation);
	}

	_recreateDebugDrawBuffer = false;
}

void PhysicsEngine::loadOneFrameDebugDrawLines()
{
	if (_oneFrameVertexList.empty())
		return;
	if (!_vertexBuffer._buffer)
		return;

	void* data;
	vmaMapMemory(_engine->_allocator, _vertexBuffer._allocation, &data);
	DebugDrawVertex* ddv = (DebugDrawVertex*)data;
	ddv += _vertexCount;  // I am starting to get used to Dmitri's presence
	memcpy(ddv, &_oneFrameVertexList[0], _oneFrameVertexList.size() * sizeof(DebugDrawVertex));
	vmaUnmapMemory(_engine->_allocator, _vertexBuffer._allocation);
}

void PhysicsEngine::debugDrawLineOneFrame(const glm::vec3& pos1, const glm::vec3& pos2, const glm::vec3& color)
{
	if (_oneFrameVertexList.size() + 2 > _oneFrameVertexListAllocation)
	{
		std::cerr << "[DEBUG DRAW LINE ONE FRAME]" << std::endl
			<< "ERROR: this new line will exceed the allocation" << std::endl
			<< "       current size of vert list: " << _oneFrameVertexList.size() << std::endl
			<< "       maximum allocation:        " << _oneFrameVertexListAllocation << std::endl;
		return;
	}
	
	_oneFrameVertexList.push_back({
		.pos = pos1,
		.physObjIndex = -1,  // -1 index for the transform... should mean "world space" or unit matrix
		.color = color,
		});
	_oneFrameVertexList.push_back({
		.pos = pos2,
		.physObjIndex = -1,
		.color = color,
		});
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

	btTransform toTransform(const glm::mat4& transform)
	{
		btTransform trans;
		trans.setFromOpenGLMatrix(glm::value_ptr(transform));
		return trans;
	}

	glm::vec3 getPosition(const glm::mat4& transform)
	{
		return glm::vec3(transform[3]);
	}

	glm::quat getRotation(const glm::mat4& transform)
	{
		// NOTE: when the scale gets larger, the quaternion will rotate up to however many dimensions there are, thus we have to scale down/normalize this transform to unit scale before extracting the quaternion
		glm::vec3 scale = getScale(transform);
		const glm::mat3 unitScaledRotationMatrix(
			glm::vec3(transform[0]) / scale[0],
			glm::vec3(transform[1]) / scale[1],
			glm::vec3(transform[2]) / scale[2]
		);
		return glm::normalize(glm::quat_cast(unitScaledRotationMatrix));		// NOTE: Seems like the quat created here needs to be normalized. Weird.  -Timo 2022-01-19
	}

	glm::vec3 getScale(const glm::mat4& transform)
	{
		glm::vec3 scale = {
			glm::length(glm::vec3(transform[0])),
			glm::length(glm::vec3(transform[1])),
			glm::length(glm::vec3(transform[2])),
		};
		return scale;
	}

	float_t lerp(const float_t& a, const float_t& b, const float_t& t)
	{
		return ((1.0f - t) * a) + (t * b);
	}

	glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, const glm::vec3& t)
	{
		return glm::vec3(lerp(a.x, b.x, t.x), lerp(a.y, b.y, t.y), lerp(a.z, b.z, t.z));
	}

	bool matrixEquals(const glm::mat4& m1, const glm::mat4& m2, float epsilon)
	{
		return (glm::abs(m1[0][0] - m2[0][0]) < epsilon &&
			glm::abs(m1[0][1] - m2[0][1]) < epsilon &&
			glm::abs(m1[0][2] - m2[0][2]) < epsilon &&
			glm::abs(m1[0][3] - m2[0][3]) < epsilon &&
			glm::abs(m1[1][0] - m2[1][0]) < epsilon &&
			glm::abs(m1[1][1] - m2[1][1]) < epsilon &&
			glm::abs(m1[1][2] - m2[1][2]) < epsilon &&
			glm::abs(m1[1][3] - m2[1][3]) < epsilon &&
			glm::abs(m1[2][0] - m2[2][0]) < epsilon &&
			glm::abs(m1[2][1] - m2[2][1]) < epsilon &&
			glm::abs(m1[2][2] - m2[2][2]) < epsilon &&
			glm::abs(m1[2][3] - m2[2][3]) < epsilon &&
			glm::abs(m1[3][0] - m2[3][0]) < epsilon &&
			glm::abs(m1[3][1] - m2[3][1]) < epsilon &&
			glm::abs(m1[3][2] - m2[3][2]) < epsilon &&
			glm::abs(m1[3][3] - m2[3][3]) < epsilon);
	}
}
