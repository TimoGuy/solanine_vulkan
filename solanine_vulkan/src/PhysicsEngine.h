#pragma once

#define BT_THREADSAFE 1
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include "Imports.h"


class VulkanEngine;
class Entity;

constexpr size_t PHYSICS_OBJECTS_MAX_CAPACITY = 1000;

struct RegisteredPhysicsObject
{
    btRigidBody* body;
    btTransform prevTransform;
    glm::vec3 transformOffset;
    glm::mat4 interpolatedTransform;    // @NOTE: these are calculated at the end of simulation
};

class PhysicsEngine
{
public:
    static PhysicsEngine& getInstance();
    
    void initialize(VulkanEngine* engine);
    void update(float_t deltaTime, std::vector<Entity*>* entities);
    void cleanup();

    float_t getGravityStrength();

    RegisteredPhysicsObject* registerPhysicsObject(float_t mass, glm::vec3 origin, glm::quat rotation, btCollisionShape* shape);    // @NOTE: setting mass=0.0 will make the rigidbody be static
    void unregisterPhysicsObject(RegisteredPhysicsObject* objRegistration);

    void renderDebugDraw(VkCommandBuffer cmd, const VkDescriptorSet& globalDescriptor);

	std::vector<VkVertexInputAttributeDescription> getVertexAttributeDescriptions();
    std::vector<VkVertexInputBindingDescription> getVertexBindingDescriptions();

private:
    btITaskScheduler* _mainTaskScheduler              = nullptr;
    btCollisionDispatcher* _dispatcher                = nullptr;
    btCollisionConfiguration* _collisionConfiguration = nullptr;
    btBroadphaseInterface* _broadphase                = nullptr;
    btConstraintSolver* _solver                       = nullptr;
    btDiscreteDynamicsWorld* _dynamicsWorld           = nullptr;

    float_t _gravityStrength = 0.0f;
    float_t _accumulatedTimeForPhysics = 0.0f;

    std::vector<RegisteredPhysicsObject> _physicsObjects;
    void calculateInterpolatedTransform(RegisteredPhysicsObject& obj, const float_t& physicsAlpha);

    btRigidBody* createRigidBody(float_t mass, const btTransform& startTransform, btCollisionShape* shape, const btVector4& color = btVector4(1, 0, 0, 1));  // @NOTE: the `shape` param looks like just one shape, but in bullet physics you need to add in a `CompoundShape` type into the shape to be able to add in multiple shapes to a single rigidbody

    struct DebugDrawVertex
    {
        glm::vec3 pos;
        int32_t physObjIndex;
    };
    VulkanEngine* _engine;

    bool _vertexBufferCreated = false;
    size_t _vertexCount;
    AllocatedBuffer _vertexBuffer;
    AllocatedBuffer _transformsBuffer;
    VkDescriptorSet _transformsDescriptor;
    void appendDebugShapeVertices(btBoxShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void appendDebugShapeVertices(btSphereShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void appendDebugShapeVertices(btCylinderShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void appendDebugShapeVertices(btCapsuleShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void recreateDebugDrawBuffer();
};

namespace physutil
{
    float_t smoothStep(float_t edge0, float_t edge1, float_t t);
	float_t moveTowards(float_t current, float_t target, float_t maxDistanceDelta);
	glm::i64 moveTowards(glm::i64 current, glm::i64 target, glm::i64 maxDistanceDelta);
	float_t moveTowardsAngle(float_t currentAngle, float_t targetAngle, float_t maxTurnDelta);
	glm::vec2 moveTowardsVec2(glm::vec2 current, glm::vec2 target, float_t maxDistanceDelta);
	glm::vec3 moveTowardsVec3(glm::vec3 current, glm::vec3 target, float_t maxDistanceDelta);
	glm::vec3 clampVector(glm::vec3 vector, float_t min, float_t max);
    btVector3 toVec3(const glm::vec3& vector);
    glm::vec3 toVec3(const btVector3& vector);
}
