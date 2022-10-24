#pragma once

#define BT_THREADSAFE 1
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>


class PhysicsEngine
{
public:
    static PhysicsEngine& getInstance();
    
    void initialize();
    void update();
    void cleanup();

private:
    btITaskScheduler* _mainTaskScheduler              = nullptr;
    btCollisionDispatcher* _dispatcher                = nullptr;
    btCollisionConfiguration* _collisionConfiguration = nullptr;
    btBroadphaseInterface* _broadphase                = nullptr;
    btConstraintSolver* _solver                       = nullptr;
    btDiscreteDynamicsWorld* _dynamicsWorld           = nullptr;
};
