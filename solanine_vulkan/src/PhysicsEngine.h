#pragma once

#include <vector>
#include <map>
#include <functional>
#define BT_THREADSAFE 1
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/NarrowPhaseCollision/btRaycastCallback.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include <glm/glm.hpp>
#include "VkDataStructures.h"


class VulkanEngine;
class Entity;

constexpr size_t PHYSICS_OBJECTS_MAX_CAPACITY = 1000;

struct RegisteredPhysicsObject
{
    // Returned values
    btRigidBody* body;
    btTransform prevTransform;
    glm::mat4 interpolatedTransform;    // @NOTE: these are calculated at the end of simulation

    // Tweakable properties
    glm::vec3 transformOffset;
    std::function<void(btPersistentManifold*, bool amIB)>* onCollisionStayCallback = nullptr;
};

class PhysicsEngine
{
public:
    static PhysicsEngine& getInstance();
    
    void initialize(VulkanEngine* engine);
    void update(float_t deltaTime, std::vector<Entity*>* entities);
    void cleanup();

    float_t getGravityStrength();
    btVector3 getGravityDirection();
    btVector3 getGravity();

    RegisteredPhysicsObject* registerPhysicsObject(float_t mass, glm::vec3 origin, glm::quat rotation, btCollisionShape* shape);    // @NOTE: setting mass=0.0 will make the rigidbody be static
    void unregisterPhysicsObject(RegisteredPhysicsObject* objRegistration);

    void lazyRecreateDebugDrawBuffer();
    void renderDebugDraw(VkCommandBuffer cmd, const VkDescriptorSet& globalDescriptor);

	std::vector<VkVertexInputAttributeDescription> getVertexAttributeDescriptions();
    std::vector<VkVertexInputBindingDescription> getVertexBindingDescriptions();

    btCollisionWorld::ClosestRayResultCallback raycast(const btVector3& from, const btVector3& to, uint32_t flags = btTriangleRaycastCallback::kF_FilterBackfaces);

private:
    btITaskScheduler* _mainTaskScheduler              = nullptr;
    btCollisionDispatcher* _dispatcher                = nullptr;
    btCollisionConfiguration* _collisionConfiguration = nullptr;
    btBroadphaseInterface* _broadphase                = nullptr;
    btConstraintSolver* _solver                       = nullptr;
    btDiscreteDynamicsWorld* _dynamicsWorld           = nullptr;

    float_t   _gravityStrength = 0.0f;
    btVector3 _gravityDirection;
    float_t   _accumulatedTimeForPhysics = 0.0f;

    std::vector<RegisteredPhysicsObject> _physicsObjects;
    void calculateInterpolatedTransform(RegisteredPhysicsObject& obj, const float_t& physicsAlpha);

    std::map<void*, RegisteredPhysicsObject*> _rigidBodyToPhysicsObjectMap;
    btRigidBody* createRigidBody(float_t mass, const btTransform& startTransform, btCollisionShape* shape, const btVector4& color = btVector4(1, 0, 0, 1));  // @NOTE: the `shape` param looks like just one shape, but in bullet physics you need to add in a `CompoundShape` type into the shape to be able to add in multiple shapes to a single rigidbody

    struct DebugDrawVertex
    {
        glm::vec3 pos;
        int32_t physObjIndex;
        glm::vec3 color;
    };
    VulkanEngine* _engine;

    bool _vertexBufferCreated = false;
    bool _recreateDebugDrawBuffer = false;
    size_t _vertexCount;
    AllocatedBuffer _vertexBuffer;
    AllocatedBuffer _transformsBuffer;
    VkDescriptorSet _transformsDescriptor;
    void appendDebugShapeVertices(btBoxShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void appendDebugShapeVertices(btSphereShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void appendDebugShapeVertices(btCylinderShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void appendDebugShapeVertices(btCapsuleShape* shape, size_t physObjIndex, std::vector<DebugDrawVertex>& vertexList);
    void recreateDebugDrawBuffer();

    const size_t _oneFrameVertexListAllocation = 10000;
    std::vector<DebugDrawVertex> _oneFrameVertexList;  // NOTE: one frame is meaning it refreshes after physics update
    void loadOneFrameDebugDrawLines();
public:
    void debugDrawLineOneFrame(const glm::vec3& pos1, const glm::vec3& pos2, const glm::vec3& color);
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
    glm::vec3 getPosition(const glm::mat4& transform);
	glm::quat getRotation(const glm::mat4& transform);
	glm::vec3 getScale(const glm::mat4& transform);
    float_t lerp(float_t a, float_t b, float_t t);
    bool matrixEquals(const glm::mat4& m1, const glm::mat4& m2, float epsilon = 0.0001f);
}
