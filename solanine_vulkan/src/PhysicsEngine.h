#pragma once

#define BT_THREADSAFE 1
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include "Imports.h"


class Entity;

struct RegisteredPhysicsObject
{
    btRigidBody* body;
    btTransform prevTransform;
    glm::mat4 interpolatedTransform;    // @NOTE: these are calculated at the end of simulation
};

class PhysicsEngine
{
public:
    static PhysicsEngine& getInstance();
    
    void initialize();
    void update(float_t deltaTime, std::vector<Entity*>* entities);
    void cleanup();

    RegisteredPhysicsObject* registerPhysicsObject(float_t mass, glm::vec3 origin, glm::quat rotation, btCollisionShape* shape);    // @NOTE: setting mass=0.0 will make the rigidbody be static
    void unregisterPhysicsObject(RegisteredPhysicsObject* objRegistration);

private:
    btITaskScheduler* _mainTaskScheduler              = nullptr;
    btCollisionDispatcher* _dispatcher                = nullptr;
    btCollisionConfiguration* _collisionConfiguration = nullptr;
    btBroadphaseInterface* _broadphase                = nullptr;
    btConstraintSolver* _solver                       = nullptr;
    btDiscreteDynamicsWorld* _dynamicsWorld           = nullptr;

    float_t accumulatedTimeForPhysics = 0.0f;
    float_t physicsInterpolationAlpha = 0.0f;

    std::vector<RegisteredPhysicsObject> _physicsObjects;
    void calculateInterpolatedTransform(RegisteredPhysicsObject& obj, const float_t& physicsAlpha);

    btRigidBody* createRigidBody(float_t mass, const btTransform& startTransform, btCollisionShape* shape, const btVector4& color = btVector4(1, 0, 0, 1));  // @NOTE: the `shape` param looks like just one shape, but in bullet physics you need to add in a `CompoundShape` type into the shape to be able to add in multiple shapes to a single rigidbody
};
