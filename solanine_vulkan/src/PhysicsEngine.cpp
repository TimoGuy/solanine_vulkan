#include "PhysicsEngine.h"

#include "Entity.h"


PhysicsEngine& PhysicsEngine::getInstance()
{
	static PhysicsEngine instance;
	return instance;
}

void PhysicsEngine::initialize()
{
	std::cout << "[INITIALIZING PHYSICS ENGINE]" << std::endl;    // @NOTE: this is gonna spit out a bunch of verbose console crap

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
	_dynamicsWorld->setGravity(btVector3(0, -10, 0));

	//
	// Create some stuff (capsule and plane)
	//
	btCollisionShape* groundShape = new btStaticPlaneShape({ 0, 1, 0 }, 0.0f);
	btCollisionShape* capsuleShape = new btCapsuleShape(0.5f, 2.0f);
}

void PhysicsEngine::update(float_t deltaTime, std::vector<Entity*>* entities)    // https://gafferongames.com/post/fix_your_timestep/
{
	constexpr float_t physicsDeltaTime = 0.02f;    // 50fps
	accumulatedTimeForPhysics += deltaTime;

	for (; accumulatedTimeForPhysics >= physicsDeltaTime; accumulatedTimeForPhysics -= physicsDeltaTime)
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
	const float_t physicsAlpha = accumulatedTimeForPhysics / physicsDeltaTime;
#ifdef _DEVELOP
	// @TODO: Make a playmode flag
	//if (!playMode)
	//	physicsInterpolationAlpha = 1.0f;
#endif

	for (size_t i = 0; i < _physicsObjects.size(); i++)    // @IMPROVEMENT: @TODO: oh man, this should definitely be multithreaded. However, taskflow doesn't seem like the best choice.  @TOOD: look into the c++11 multithreaded for loop
		calculateInterpolatedTransform(_physicsObjects[i], physicsAlpha);
	std::cout << std::endl;
}

void PhysicsEngine::cleanup()
{
	delete _mainTaskScheduler;    // @NOTE: apparently this is all you need?
}

RegisteredPhysicsObject* PhysicsEngine::registerPhysicsObject(float_t mass, glm::vec3 origin, glm::quat rotation, btCollisionShape* shape)
{
	glm::mat4 glmTrans = glm::translate(glm::mat4(1.0f), origin) * glm::toMat4(rotation);

	btTransform trans;
	trans.setFromOpenGLMatrix(glm::value_ptr(glmTrans));

	RegisteredPhysicsObject rpo = {
		.body = createRigidBody(mass, trans, shape),
		.prevTransform = trans,    // Set it to this so there's a basis to do the interpolation from
		.interpolatedTransform = glmTrans,
	};
	_physicsObjects.push_back(rpo);
	//return &_physicsObjects.back();
	return &_physicsObjects[_physicsObjects.size() - 1];
}

void PhysicsEngine::unregisterPhysicsObject(RegisteredPhysicsObject* objRegistration)
{
	std::erase_if(_physicsObjects,
		[=](RegisteredPhysicsObject& x) {
			return &x == objRegistration;
		}
	);
}

void PhysicsEngine::calculateInterpolatedTransform(RegisteredPhysicsObject& obj, const float_t& physicsAlpha)
{
	btTransform currentTransform = obj.body->getWorldTransform();
	//btTransform interpolatedTransform(
	//	obj.prevTransform.getRotation().slerp(currentTransform.getRotation(), physicsAlpha),    // NLerp nor lerp are available for btQuaternions smh
	//	obj.prevTransform.getOrigin().lerp(currentTransform.getOrigin(), physicsAlpha)
	//);
	//interpolatedTransform.getOpenGLMatrix(glm::value_ptr(obj.interpolatedTransform));  // Apply to the interpolatedTransform matrix!
	glm::mat4 newTrans;
	currentTransform.getOpenGLMatrix(glm::value_ptr(newTrans));  // Apply to the interpolatedTransform matrix!
	obj.interpolatedTransform = newTrans;
	auto koko = currentTransform.getOrigin();
	std::cout << koko.getX() << "," << koko.getY() << "," << koko.getZ() << "\t";
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
