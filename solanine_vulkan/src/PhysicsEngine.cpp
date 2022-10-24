#include "PhysicsEngine.h"

#include "Imports.h"


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
			// @NOTE: Pool solvers shouldn't be parallel solvers, we don't allow that kind of
			// nested parallelism because of performance issues  -Bullet3 example project
			solvers[i] = new btSequentialImpulseConstraintSolver();
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

void PhysicsEngine::update()
{}

void PhysicsEngine::cleanup()
{
	delete _mainTaskScheduler;
}
