#include "pch.h"

#include "PhysicsEngine.h"

#ifdef _DEVELOP
#include "VkDataStructures.h"
#include "VulkanEngine.h"
#include "VkDescriptorBuilderUtil.h"
#include "VkPipelineBuilderUtil.h"
#include "VkInitializers.h"
#include "Camera.h"
#include "Debug.h"
#endif

#include "PhysUtil.h"
#include "InputManager.h"
#include "EntityManager.h"
#include "SimulationCharacter.h"
#include "GlobalState.h"
#include "CombatInteractionManager.h"
#include "VkglTFModel.h"

JPH_SUPPRESS_WARNINGS
using namespace JPH;
using namespace JPH::literals;


namespace physengine
{
    bool isInitialized = false;

    //
    // Physics engine works
    //
    constexpr float_t simDeltaTime = 0.025f;    // 40fps. This seemed to be the sweet spot. 25/30fps would be inconsistent for getting smaller platform jumps with the dash move. 50fps felt like too many physics calculations all at once. 40fps seems right, striking a balance.  -Timo 2023/01/26
    constexpr float_t physicsDeltaTimeInMS = simDeltaTime * 1000.0f;
    constexpr float_t oneOverPhysicsDeltaTimeInMS = 1.0f / physicsDeltaTimeInMS;

    constexpr float_t collisionTolerance = 0.05f;  // For physics characters.

    void runPhysicsEngineAsync();
    EntityManager* entityManager;
    bool isAsyncRunnerRunning;
    std::thread* asyncRunner = nullptr;
    uint64_t lastTick;

    bool runPhysicsSimulations = false;

    PhysicsSystem* physicsSystem = nullptr;
    std::unordered_map<JPH::BodyID, std::string> bodyIdToEntityGuidMap;

    enum class UserDataMeaning
    {
        NOTHING = 0,
        IS_CHARACTER,
    };

    // Simulation transform.
    struct SimulationTransform
    {
        vec3 position = GLM_VEC3_ZERO_INIT;
        versor rotation = GLM_QUAT_IDENTITY_INIT;
    };
    struct SimulationInterpolationSet
    {
        SimulationTransform simTransforms[65536 * 2];
    };
    // @NOTE: The interpolated simulation position is calculated between `prevSimSet` and `currentSimSet` and is stored in `calcInterpolatedSet`.
    //        New simulation transforms are written to `nextSimSet` as the calculations are formed. Once the "tick" or new frame has started,
    //        `transformSwap()` pushes `nextSimSet` to `currentSimSet` which gets pushed to `prevSimSet`. And so, more writing can happen with
    //        as little blocking the render thread as possible.  -Timo 2023/11/20
    // @AMEND: I'm changing this so that there's no separate pointers for prev, current, and next sim sets. Now it will be an atomic size_t (`simSetOffset`) that
    //         ticks the new simulation sets into place. Using `simSetChain`, `simSetOffset + 0` is prev sim set, `simSetOffset + 1` is current sim set, and
    //         `simSetOffset + 2` is next sim set. This method should remove the need for the mutex that does the pointer swap.  -Timo 2023/12/28
    SimulationInterpolationSet* simSetChain[3] = {
        nullptr,
        nullptr,
        nullptr,
    };
    std::atomic<size_t> simSetOffset = 0;
    SimulationInterpolationSet* calcInterpolatedSet = nullptr;
    std::vector<size_t> registeredSimSetIndices;
    std::mutex mutateSimSetPoolsMutex;

#ifdef _DEVELOP
    struct DebugStats
    {
        size_t simTimesUSHeadIndex = 0;
        size_t simTimesUSCount = 256;
        float_t simTimesUS[256 * 2];
        float_t highestSimTime = -1.0f;
    } perfStats;

    //
    // Debug visualization
    // @INCOMPLETE: for now just have capsules and raycasts be visualized, since the 3d models for the voxel fields is an accurate visualization of it anyways.  -Timo 2023/06/13
    //
    VulkanEngine* engine;
    struct GPUVisCameraData
    {
        mat4 projectionView;
    };
    AllocatedBuffer visCameraBuffer;

    struct GPUVisInstancePushConst
    {
        vec4 color1;
        vec4 color2;
        vec4 pt1;  // Vec4's for padding.
        vec4 pt2;
        mat4 capsuleRotation = GLM_MAT4_IDENTITY_INIT;
        float_t capsuleRadius;
    };

    struct DebugVisVertex
    {
        vec3 pos;
        int32_t pointSpace;  // 0 is pt1 space. 1 is pt2 space.
    };
    AllocatedBuffer capsuleVisVertexBuffer;  // @NOTE: there will be vertex attributes that will set a vertex to pt1's transform or pt2's transform.
    AllocatedBuffer lineVisVertexBuffer;
    uint32_t capsuleVisVertexCount;
    uint32_t lineVisVertexCount;
    bool vertexBuffersInitialized = false;

    struct DebugVisLine
    {
        vec3 pt1, pt2;
        DebugVisLineType type;
    };
    std::vector<DebugVisLine> debugVisLines;
    std::mutex mutateDebugVisLines;

    void initializeAndUploadBuffers()
    {
        static std::vector<DebugVisVertex> capsuleVertices = {
            // Bottom cap, y-plane circle.
            { {  1.000f, 0.0f,  0.000f }, 0 }, { {  0.924f, 0.0f,  0.383f }, 0 },
            { {  0.924f, 0.0f,  0.383f }, 0 }, { {  0.707f, 0.0f,  0.707f }, 0 },
            { {  0.707f, 0.0f,  0.707f }, 0 }, { {  0.383f, 0.0f,  0.924f }, 0 },
            { {  0.383f, 0.0f,  0.924f }, 0 }, { {  0.000f, 0.0f,  1.000f }, 0 },
            { {  0.000f, 0.0f,  1.000f }, 0 }, { { -0.383f, 0.0f,  0.924f }, 0 },
            { { -0.383f, 0.0f,  0.924f }, 0 }, { { -0.707f, 0.0f,  0.707f }, 0 },
            { { -0.707f, 0.0f,  0.707f }, 0 }, { { -0.924f, 0.0f,  0.383f }, 0 },
            { { -0.924f, 0.0f,  0.383f }, 0 }, { { -1.000f, 0.0f,  0.000f }, 0 },
            { { -1.000f, 0.0f,  0.000f }, 0 }, { { -0.924f, 0.0f, -0.383f }, 0 },
            { { -0.924f, 0.0f, -0.383f }, 0 }, { { -0.707f, 0.0f, -0.707f }, 0 },
            { { -0.707f, 0.0f, -0.707f }, 0 }, { { -0.383f, 0.0f, -0.924f }, 0 },
            { { -0.383f, 0.0f, -0.924f }, 0 }, { {  0.000f, 0.0f, -1.000f }, 0 },
            { {  0.000f, 0.0f, -1.000f }, 0 }, { {  0.383f, 0.0f, -0.924f }, 0 },
            { {  0.383f, 0.0f, -0.924f }, 0 }, { {  0.707f, 0.0f, -0.707f }, 0 },
            { {  0.707f, 0.0f, -0.707f }, 0 }, { {  0.924f, 0.0f, -0.383f }, 0 },
            { {  0.924f, 0.0f, -0.383f }, 0 }, { {  1.000f, 0.0f,  0.000f }, 0 },

            // Top cap, y-plane circle.
            { {  1.000f, 0.0f,  0.000f }, 1 }, { {  0.924f, 0.0f,  0.383f }, 1 },
            { {  0.924f, 0.0f,  0.383f }, 1 }, { {  0.707f, 0.0f,  0.707f }, 1 },
            { {  0.707f, 0.0f,  0.707f }, 1 }, { {  0.383f, 0.0f,  0.924f }, 1 },
            { {  0.383f, 0.0f,  0.924f }, 1 }, { {  0.000f, 0.0f,  1.000f }, 1 },
            { {  0.000f, 0.0f,  1.000f }, 1 }, { { -0.383f, 0.0f,  0.924f }, 1 },
            { { -0.383f, 0.0f,  0.924f }, 1 }, { { -0.707f, 0.0f,  0.707f }, 1 },
            { { -0.707f, 0.0f,  0.707f }, 1 }, { { -0.924f, 0.0f,  0.383f }, 1 },
            { { -0.924f, 0.0f,  0.383f }, 1 }, { { -1.000f, 0.0f,  0.000f }, 1 },
            { { -1.000f, 0.0f,  0.000f }, 1 }, { { -0.924f, 0.0f, -0.383f }, 1 },
            { { -0.924f, 0.0f, -0.383f }, 1 }, { { -0.707f, 0.0f, -0.707f }, 1 },
            { { -0.707f, 0.0f, -0.707f }, 1 }, { { -0.383f, 0.0f, -0.924f }, 1 },
            { { -0.383f, 0.0f, -0.924f }, 1 }, { {  0.000f, 0.0f, -1.000f }, 1 },
            { {  0.000f, 0.0f, -1.000f }, 1 }, { {  0.383f, 0.0f, -0.924f }, 1 },
            { {  0.383f, 0.0f, -0.924f }, 1 }, { {  0.707f, 0.0f, -0.707f }, 1 },
            { {  0.707f, 0.0f, -0.707f }, 1 }, { {  0.924f, 0.0f, -0.383f }, 1 },
            { {  0.924f, 0.0f, -0.383f }, 1 }, { {  1.000f, 0.0f,  0.000f }, 1 },

            // X-plane circle.
            { { 0.0f,  1.000f,  0.000f }, 1 }, { { 0.0f,  0.924f,  0.383f }, 1 },
            { { 0.0f,  0.924f,  0.383f }, 1 }, { { 0.0f,  0.707f,  0.707f }, 1 },
            { { 0.0f,  0.707f,  0.707f }, 1 }, { { 0.0f,  0.383f,  0.924f }, 1 },
            { { 0.0f,  0.383f,  0.924f }, 1 }, { { 0.0f,  0.000f,  1.000f }, 1 },
            { { 0.0f,  0.000f,  1.000f }, 0 }, { { 0.0f, -0.383f,  0.924f }, 0 },
            { { 0.0f, -0.383f,  0.924f }, 0 }, { { 0.0f, -0.707f,  0.707f }, 0 },
            { { 0.0f, -0.707f,  0.707f }, 0 }, { { 0.0f, -0.924f,  0.383f }, 0 },
            { { 0.0f, -0.924f,  0.383f }, 0 }, { { 0.0f, -1.000f,  0.000f }, 0 },
            { { 0.0f, -1.000f,  0.000f }, 0 }, { { 0.0f, -0.924f, -0.383f }, 0 },
            { { 0.0f, -0.924f, -0.383f }, 0 }, { { 0.0f, -0.707f, -0.707f }, 0 },
            { { 0.0f, -0.707f, -0.707f }, 0 }, { { 0.0f, -0.383f, -0.924f }, 0 },
            { { 0.0f, -0.383f, -0.924f }, 0 }, { { 0.0f,  0.000f, -1.000f }, 0 },
            { { 0.0f,  0.000f, -1.000f }, 1 }, { { 0.0f,  0.383f, -0.924f }, 1 },
            { { 0.0f,  0.383f, -0.924f }, 1 }, { { 0.0f,  0.707f, -0.707f }, 1 },
            { { 0.0f,  0.707f, -0.707f }, 1 }, { { 0.0f,  0.924f, -0.383f }, 1 },
            { { 0.0f,  0.924f, -0.383f }, 1 }, { { 0.0f,  1.000f,  0.000f }, 1 },

            // Z-plane circle.
            { {  1.000f,  0.000f, 0.0f }, 1 }, { {  0.924f,  0.383f, 0.0f }, 1 },
            { {  0.924f,  0.383f, 0.0f }, 1 }, { {  0.707f,  0.707f, 0.0f }, 1 },
            { {  0.707f,  0.707f, 0.0f }, 1 }, { {  0.383f,  0.924f, 0.0f }, 1 },
            { {  0.383f,  0.924f, 0.0f }, 1 }, { {  0.000f,  1.000f, 0.0f }, 1 },
            { {  0.000f,  1.000f, 0.0f }, 1 }, { { -0.383f,  0.924f, 0.0f }, 1 },
            { { -0.383f,  0.924f, 0.0f }, 1 }, { { -0.707f,  0.707f, 0.0f }, 1 },
            { { -0.707f,  0.707f, 0.0f }, 1 }, { { -0.924f,  0.383f, 0.0f }, 1 },
            { { -0.924f,  0.383f, 0.0f }, 1 }, { { -1.000f,  0.000f, 0.0f }, 1 },
            { { -1.000f,  0.000f, 0.0f }, 0 }, { { -0.924f, -0.383f, 0.0f }, 0 },
            { { -0.924f, -0.383f, 0.0f }, 0 }, { { -0.707f, -0.707f, 0.0f }, 0 },
            { { -0.707f, -0.707f, 0.0f }, 0 }, { { -0.383f, -0.924f, 0.0f }, 0 },
            { { -0.383f, -0.924f, 0.0f }, 0 }, { {  0.000f, -1.000f, 0.0f }, 0 },
            { {  0.000f, -1.000f, 0.0f }, 0 }, { {  0.383f, -0.924f, 0.0f }, 0 },
            { {  0.383f, -0.924f, 0.0f }, 0 }, { {  0.707f, -0.707f, 0.0f }, 0 },
            { {  0.707f, -0.707f, 0.0f }, 0 }, { {  0.924f, -0.383f, 0.0f }, 0 },
            { {  0.924f, -0.383f, 0.0f }, 0 }, { {  1.000f,  0.000f, 0.0f }, 0 },

            // Connecting lines.
            { { -1.0f, 0.0f,  0.0f }, 0 }, { { -1.0f, 0.0f,  0.0f }, 1 },
            { {  1.0f, 0.0f,  0.0f }, 0 }, { {  1.0f, 0.0f,  0.0f }, 1 },
            { {  0.0f, 0.0f, -1.0f }, 0 }, { {  0.0f, 0.0f, -1.0f }, 1 },
            { {  0.0f, 0.0f,  1.0f }, 0 }, { {  0.0f, 0.0f,  1.0f }, 1 },
        };
        size_t capsuleVerticesSize = sizeof(DebugVisVertex) * capsuleVertices.size();
        AllocatedBuffer    cUp = engine->createBuffer(capsuleVerticesSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        capsuleVisVertexBuffer = engine->createBuffer(capsuleVerticesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        capsuleVisVertexCount = (uint32_t)capsuleVertices.size();

        static std::vector<DebugVisVertex> lineVertices = {
            { { 0.0f, 0.0f, 0.0f }, 0 }, { { 0.0f, 0.0f, 0.0f }, 1 },
        };
        size_t lineVerticesSize = sizeof(DebugVisVertex) * lineVertices.size();
        AllocatedBuffer lUp = engine->createBuffer(lineVerticesSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        lineVisVertexBuffer = engine->createBuffer(lineVerticesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        lineVisVertexCount = (uint32_t)lineVertices.size();

        void* data;
        vmaMapMemory(engine->_allocator, cUp._allocation, &data);
        memcpy(data, capsuleVertices.data(), capsuleVerticesSize);
        vmaUnmapMemory(engine->_allocator, cUp._allocation);
        vmaMapMemory(engine->_allocator, lUp._allocation, &data);
        memcpy(data, lineVertices.data(), lineVerticesSize);
        vmaUnmapMemory(engine->_allocator, lUp._allocation);

        engine->immediateSubmit([&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = capsuleVerticesSize,
            };
            vkCmdCopyBuffer(cmd, cUp._buffer, capsuleVisVertexBuffer._buffer, 1, &copyRegion);

            copyRegion.size = lineVerticesSize;
            vkCmdCopyBuffer(cmd, lUp._buffer, lineVisVertexBuffer._buffer, 1, &copyRegion);
            });

        vmaDestroyBuffer(engine->_allocator, cUp._buffer, cUp._allocation);
        vmaDestroyBuffer(engine->_allocator, lUp._buffer, lUp._allocation);

        visCameraBuffer = engine->createBuffer(sizeof(GPUVisCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    VkDescriptorSet debugVisDescriptor;
    VkDescriptorSetLayout debugVisDescriptorLayout;

    void initDebugVisDescriptors(VulkanEngine* engineRef)
    {
        engine = engineRef;
        if (!vertexBuffersInitialized)
        {
            initializeAndUploadBuffers();
            vertexBuffersInitialized = true;
        }

        VkDescriptorBufferInfo debugVisCameraInfo = {
            .buffer = visCameraBuffer._buffer,
            .offset = 0,
            .range = sizeof(GPUVisCameraData),
        };

        vkutil::DescriptorBuilder::begin()
            .bindBuffer(0, &debugVisCameraInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .build(debugVisDescriptor, debugVisDescriptorLayout);
    }

    VkPipeline debugVisPipeline = VK_NULL_HANDLE;
    VkPipelineLayout debugVisPipelineLayout;

    void initDebugVisPipelines(VkRenderPass mainRenderPass, VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor, DeletionQueue& deletionQueue)
    {
        // Setup vertex descriptions
        VkVertexInputAttributeDescription posAttribute = {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(DebugVisVertex, pos),
        };
        VkVertexInputAttributeDescription pointSpaceAttribute = {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32_SINT,
            .offset = offsetof(DebugVisVertex, pointSpace),
        };
        std::vector<VkVertexInputAttributeDescription> attributes = { posAttribute, pointSpaceAttribute };

        VkVertexInputBindingDescription mainBinding = {
            .binding = 0,
            .stride = sizeof(DebugVisVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        std::vector<VkVertexInputBindingDescription> bindings = { mainBinding };

        // Build pipeline
        vkutil::pipelinebuilder::build(
            {
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    .offset = 0,
                    .size = sizeof(GPUVisInstancePushConst)
                }
            },
            { debugVisDescriptorLayout },
            {
                { VK_SHADER_STAGE_VERTEX_BIT, "res/shaders/physengineDebugVis.vert.spv" },
                { VK_SHADER_STAGE_FRAGMENT_BIT, "res/shaders/physengineDebugVis.frag.spv" },
            },
            attributes,
            bindings,
            vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_LINE_LIST),
            screenspaceViewport,
            screenspaceScissor,
            vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE),
            { vkinit::colorBlendAttachmentState() },
            vkinit::multisamplingStateCreateInfo(),
            vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_NEVER),
            {},
            mainRenderPass,
            1,
            debugVisPipeline,
            debugVisPipelineLayout,
            deletionQueue
        );
    }

    void savePhysicsWorldSnapshot()
    {
        // Convert physics system to scene
        Ref<PhysicsScene> scene = new PhysicsScene();
        scene->FromPhysicsSystem(physicsSystem);

        // Save scene
        std::ofstream stream("physics_world_snapshot.bin", std::ofstream::out | std::ofstream::trunc | std::ofstream::binary);
        StreamOutWrapper wrapper(stream);
        if (stream.is_open())
            scene->SaveBinaryState(wrapper, true, true);
    }
#endif

    void start(EntityManager* em)
    {
        entityManager = em;
        isAsyncRunnerRunning = true;
        asyncRunner = new std::thread(runPhysicsEngineAsync);
    }

    void haltAsyncRunner()
    {
        isAsyncRunnerRunning = false;
        asyncRunner->join();
    }

    void cleanup()
    {
        comim::cleanup();

        delete simSetChain[0];
        delete simSetChain[1];
        delete simSetChain[2];
        delete calcInterpolatedSet;

        delete asyncRunner;
        delete physicsSystem;

        UnregisterTypes();

        delete Factory::sInstance;
        Factory::sInstance = nullptr;

#ifdef _DEVELOP
        vmaDestroyBuffer(engine->_allocator, visCameraBuffer._buffer, visCameraBuffer._allocation);
        vmaDestroyBuffer(engine->_allocator, capsuleVisVertexBuffer._buffer, capsuleVisVertexBuffer._allocation);
        vmaDestroyBuffer(engine->_allocator, lineVisVertexBuffer._buffer, lineVisVertexBuffer._allocation);
#endif
    }

    void requestSetRunPhysicsSimulation(bool flag)
    {
        runPhysicsSimulations = flag;
    }

    bool getIsRunPhysicsSimulation()
    {
        return runPhysicsSimulations;
    }

    size_t registerSimulationTransform()
    {
        std::lock_guard<std::mutex> lg(mutateSimSetPoolsMutex);

        size_t proposedId;
        for (proposedId = 0; proposedId < 65536 * 2; proposedId++)
        {
            bool collided = false;
            for (auto& i : registeredSimSetIndices)
                if (i == proposedId)
                {
                    collided = true;
                    break;
                }
            
            if (!collided)
            {
                // Register this one as a simulation transform.
                glm_vec3_zero(simSetChain[0]->simTransforms[proposedId].position);
                glm_vec3_zero(simSetChain[1]->simTransforms[proposedId].position);
                glm_vec3_zero(simSetChain[2]->simTransforms[proposedId].position);
                glm_vec3_zero(calcInterpolatedSet->simTransforms[proposedId].position);
                glm_quat_identity(simSetChain[0]->simTransforms[proposedId].rotation);
                glm_quat_identity(simSetChain[1]->simTransforms[proposedId].rotation);
                glm_quat_identity(simSetChain[2]->simTransforms[proposedId].rotation);
                glm_quat_identity(calcInterpolatedSet->simTransforms[proposedId].rotation);
                registeredSimSetIndices.push_back(proposedId);
                std::sort(registeredSimSetIndices.begin(), registeredSimSetIndices.end());
                return proposedId;
            }
        }

        std::cerr << "[REGISTER SIMULATION TRANSFORM]" << std::endl
            << "ERROR: no more transforms available to register. The pool is full." << std::endl;
        return (size_t)-1;
    }

    void unregisterSimulationTransform(size_t id)
    {
        std::lock_guard<std::mutex> lg(mutateSimSetPoolsMutex);
        
        for (auto it = registeredSimSetIndices.begin(); it != registeredSimSetIndices.end(); it++)
            if ((*it) == id)
            {
                registeredSimSetIndices.erase(it);
                return;
            }

        std::cerr << "[UNREGISTER SIMULATION TRANSFORM]" << std::endl
            << "WARNING: id " << id << " was not found in pool to delete. It did not exist." << std::endl;
    }

    void updateSimulationTransformPosition(size_t id, vec3 pos)
    {
        size_t simSetOffsetCopy = simSetOffset;
        size_t nextSimSet = (simSetOffsetCopy + 2) % 3;
        glm_vec3_copy(pos, simSetChain[nextSimSet]->simTransforms[id].position);
    }

    void updateSimulationTransformRotation(size_t id, versor rot)
    {
        size_t simSetOffsetCopy = simSetOffset;
        size_t nextSimSet = (simSetOffsetCopy + 2) % 3;
        glm_quat_copy(rot, simSetChain[nextSimSet]->simTransforms[id].rotation);
    }

    void getInterpSimulationTransformPosition(size_t id, vec3& outPos)
    {
        glm_vec3_copy(calcInterpolatedSet->simTransforms[id].position, outPos);
    }

    void getInterpSimulationTransformRotation(size_t id, versor& outRot)
    {
        glm_quat_copy(calcInterpolatedSet->simTransforms[id].rotation, outRot);
    }

    void getCurrentSimulationTransformPositionAndRotation(size_t id, vec3& outPos, versor& outRot)
    {
        auto& transform = simSetChain[(simSetOffset % 3)]->simTransforms[id];
        glm_vec3_copy(transform.position, outPos);
        glm_quat_copy(transform.rotation, outRot);
    }

    void transformSwap();
    void copyResultTransforms();

    static void TraceImpl(const char* inFMT, ...)  // Callback for traces, connect this to your own trace function if you have one
    {
        // Format the message
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);

        // Print to the TTY
        std::cout << buffer << std::endl;
    }

#ifdef JPH_ENABLE_ASSERTS

    // Callback for asserts, connect this to your own assert handler if you have one
    static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine)
    {
        // Print to the TTY
        std::cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "") << std::endl;

        // Breakpoint
        return true;
    };

#endif // JPH_ENABLE_ASSERTS

    // Layer that objects can be in, determines which other objects it can collide with
    // Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
    // layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
    // but only if you do collision testing).
    namespace Layers
    {
        static constexpr ObjectLayer NON_MOVING = 0;
        static constexpr ObjectLayer MOVING = 1;
        static constexpr ObjectLayer HIT_HURT_BOX = 2;
        static constexpr ObjectLayer NUM_LAYERS = 3;
    };

    /// Class that determines if two object layers can collide
    class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
            case Layers::NON_MOVING:
                return
                    inObject2 == Layers::MOVING &&  // Non moving only collides with moving.
                    inObject2 != Layers::HIT_HURT_BOX;
            case Layers::MOVING:
                return
                    true && // Moving collides with everything (except hit hurt box).
                    inObject2 != Layers::HIT_HURT_BOX;
            case Layers::HIT_HURT_BOX:
                return false;  // Hit/hurt boxes only do scene queries, so no collision.
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    // Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
    // a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
    // You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
    // many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
    // your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
    namespace BroadPhaseLayers
    {
        static constexpr BroadPhaseLayer NON_MOVING(0);
        static constexpr BroadPhaseLayer MOVING(1);
        static constexpr BroadPhaseLayer HIT_HURT_BOX(2);
        static constexpr uint32_t NUM_LAYERS(3);
    };

    // BroadPhaseLayerInterface implementation
    // This defines a mapping between object and broadphase layers.
    class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
    {
    public:
        BPLayerInterfaceImpl()
        {
            // Create a mapping table from object to broad phase layer
            mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
            mObjectToBroadPhase[Layers::HIT_HURT_BOX] = BroadPhaseLayers::HIT_HURT_BOX;
        }

        virtual uint32_t GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
        {
            JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
            return mObjectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
        {
            switch ((BroadPhaseLayer::Type)inLayer)
            {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::HIT_HURT_BOX: return "HIT_HURT_BOX";
            default:													JPH_ASSERT(false); return "INVALID";
            }
        }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

    private:
        BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    /// Class that determines if an object layer can collide with a broadphase layer
    class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
        {
            switch (inLayer1)
            {
            case Layers::NON_MOVING:
                return
                    inLayer2 == BroadPhaseLayers::MOVING &&
                    inLayer2 != BroadPhaseLayers::HIT_HURT_BOX;
            case Layers::MOVING:
                return
                    true &&
                    inLayer2 != BroadPhaseLayers::HIT_HURT_BOX;
            case Layers::HIT_HURT_BOX:
                return false;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    // An example contact listener
    class MyContactListener : public ContactListener
    {
    public:
        // See: ContactListener
        virtual ValidateResult OnContactValidate(const Body& inBody1, const Body& inBody2, RVec3Arg inBaseOffset, const CollideShapeResult& inCollisionResult) override
        {
            //std::cout << "Contact validate callback" << std::endl;

            // Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
            return ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        void processUserDataMeaning(const Body& thisBody, const Body& otherBody, const ContactManifold& manifold, ContactSettings& ioSettings)
        {
            switch (UserDataMeaning(thisBody.GetUserData()))
            {
                case UserDataMeaning::NOTHING:
                    return;
                
                case UserDataMeaning::IS_CHARACTER:
                {
                    SimulationCharacter* entityAsChar;
                    if (entityAsChar = dynamic_cast<SimulationCharacter*>(entityManager->getEntityViaGUID(bodyIdToEntityGuidMap[thisBody.GetID()])))
                        entityAsChar->reportPhysicsContact(otherBody, manifold, &ioSettings);
                } return;
            }
        }

        virtual void OnContactAdded(const Body& inBody1, const Body& inBody2, const ContactManifold& inManifold, ContactSettings& ioSettings) override
        {
            //std::cout << "A contact was added" << std::endl;
            processUserDataMeaning(inBody1, inBody2, inManifold, ioSettings);
            processUserDataMeaning(inBody2, inBody1, inManifold.SwapShapes(), ioSettings);
        }

        virtual void OnContactPersisted(const Body& inBody1, const Body& inBody2, const ContactManifold& inManifold, ContactSettings& ioSettings) override
        {
            //std::cout << "A contact was persisted" << std::endl;
            processUserDataMeaning(inBody1, inBody2, inManifold, ioSettings);
            processUserDataMeaning(inBody2, inBody1, inManifold.SwapShapes(), ioSettings);
        }

        virtual void OnContactRemoved(const SubShapeIDPair& inSubShapePair) override
        {
            //std::cout << "A contact was removed" << std::endl;
        }
    } contactListener;

    // An example activation listener
    class MyBodyActivationListener : public BodyActivationListener
    {
    public:
        virtual void OnBodyActivated(const BodyID& inBodyID, uint64_t inBodyUserData) override
        {
            //std::cout << "A body got activated" << std::endl;
        }

        virtual void OnBodyDeactivated(const BodyID& inBodyID, uint64_t inBodyUserData) override
        {
            //std::cout << "A body went to sleep" << std::endl;
        }
    } bodyActivationListener;

    void runPhysicsEngineAsync()
    {
        tracy::SetThreadName("Simulation Thread");

        //
        // Init Physics World.
        // REFERENCE: https://github.com/jrouwe/JoltPhysics/blob/master/HelloWorld/HelloWorld.cpp
        //
        RegisterDefaultAllocator();

        Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl);

        Factory::sInstance = new Factory();
        RegisterTypes();

        TempAllocatorImpl tempAllocator(10 * 1024 * 1024);

        constexpr int32_t maxPhysicsJobs = 2048;
        constexpr int32_t maxPhysicsBarriers = 8;
        JobSystemThreadPool jobSystem(maxPhysicsJobs, maxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

        const uint32_t maxBodies = 65536;
        const uint32_t numBodyMutexes = 0;  // Default settings is no mutexes to protect bodies from concurrent access.
        const uint32_t maxBodyPairs = 65536;
        const uint32_t maxContactConstraints = 10240;

        BPLayerInterfaceImpl broadphaseLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphaseLayerFilter;
        ObjectLayerPairFilterImpl objectVsObjectLayerFilter;

        physicsSystem = new PhysicsSystem;
        physicsSystem->Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints, broadphaseLayerInterface, objectVsBroadphaseLayerFilter, objectVsObjectLayerFilter);
        physicsSystem->SetBodyActivationListener(&bodyActivationListener);
        physicsSystem->SetContactListener(&contactListener);
        setWorldGravity(vec3{ 0.0f, -37.5f, 0.0f });  // @NOTE: This is tuned to be the original.  -Timo 2023/09/29

        physicsSystem->OptimizeBroadPhase();

        simSetChain[0] = new SimulationInterpolationSet;
        simSetChain[1] = new SimulationInterpolationSet;
        simSetChain[2] = new SimulationInterpolationSet;
        calcInterpolatedSet = new SimulationInterpolationSet;

        comim::init();

#ifdef _DEVELOP
        input::registerEditorInputSetOnThisThread();
#endif

        isInitialized = true;  // Initialization finished.

        //
        // Run Physics Simulation until no more.
        //
        while (isAsyncRunnerRunning)
        {
            lastTick = SDL_GetTicks64();

#ifdef _DEVELOP
            {   // Reset all the debug vis lines.
                std::lock_guard<std::mutex> lg(mutateDebugVisLines);
                debugVisLines.clear();
            }

            uint64_t perfTime = SDL_GetPerformanceCounter();
#endif

            if (!globalState::isEditingMode &&
                input::editorInputSet().playModeToggleSimulation.onAction)
            {
                runPhysicsSimulations = !runPhysicsSimulations;
                debug::pushDebugMessage({
                    .message = std::string("Set running physics simulations to ") + (runPhysicsSimulations ? "on" : "off"),
                    });
            }

            // @NOTE: this is the only place where `timeScale` is used. That's
            //        because this system is designed to be running at 40fps constantly
            //        in real time, so it doesn't slow down or speed up with time scale.
            // @REPLY: I thought that the system should just run in a constant 40fps. As in,
            //         if the timescale slows down, then the tick rate should also slow down
            //         proportionate to the timescale.  -Timo 2023/06/10
            transformSwap();
            
            input::editorInputSet().update();
            input::simInputSet().update(simDeltaTime);
            entityManager->INTERNALsimulationUpdate(simDeltaTime);  // @NOTE: if timescale changes, then the system just waits longer/shorter per loop.
            comim::simulationTick();

            if (runPhysicsSimulations)
            {
                ZoneScopedN("Update Jolt phys sys");
                physicsSystem->Update(simDeltaTime, 1, 1, &tempAllocator, &jobSystem);
            }
            copyResultTransforms();

#ifdef _DEVELOP
            {
                ZoneScopedN("Update performance metrics");

                //
                // Update performance metrics
                // @COPYPASTA
                //
                perfTime = SDL_GetPerformanceCounter() - perfTime;
                perfStats.simTimesUSHeadIndex = (size_t)std::fmodf((float_t)perfStats.simTimesUSHeadIndex + 1, (float_t)perfStats.simTimesUSCount);

                // Find what the highest simulation time is
                if (perfTime > perfStats.highestSimTime)
                    perfStats.highestSimTime = perfTime;
                else if (perfStats.simTimesUS[perfStats.simTimesUSHeadIndex] == perfStats.highestSimTime)
                {
                    // Former highest sim time is getting overwritten; recalculate the 2nd highest sim time.
                    float_t nextHighestsimTime = perfTime;
                    for (size_t i = perfStats.simTimesUSHeadIndex + 1; i < perfStats.simTimesUSHeadIndex + perfStats.simTimesUSCount; i++)
                        nextHighestsimTime = std::max(nextHighestsimTime, perfStats.simTimesUS[i]);
                    perfStats.highestSimTime = nextHighestsimTime;
                }

                // Apply simulation time to buffer
                perfStats.simTimesUS[perfStats.simTimesUSHeadIndex] =
                    perfStats.simTimesUS[perfStats.simTimesUSHeadIndex + perfStats.simTimesUSCount] =
                    perfTime;
            }
#endif

            // Wait for remaining time
            uint64_t endingTime = SDL_GetTicks64();
            uint64_t timeDiff = endingTime - lastTick;
            uint64_t physicsDeltaTimeInMSScaled = physicsDeltaTimeInMS / globalState::timescale;
            if (timeDiff > physicsDeltaTimeInMSScaled)
            {
                std::cerr << "ERROR: physics engine is running too slowly. (" << (timeDiff - physicsDeltaTimeInMSScaled) << "ns behind)" << std::endl;
            }
            else
            {
                SDL_Delay((uint32_t)(physicsDeltaTimeInMSScaled - timeDiff));
            }
        }
    }

    //
    // Voxel field pool
    //
    constexpr size_t PHYSICS_OBJECTS_MAX_CAPACITY = 10000;

    VoxelFieldPhysicsData voxelFieldPool[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t voxelFieldIndices[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t numVFsCreated = 0;

    VoxelFieldPhysicsData* createVoxelField(const std::string& entityGuid, mat4 transform, const size_t& sizeX, const size_t& sizeY, const size_t& sizeZ, uint8_t* voxelData)
    {
        if (numVFsCreated < PHYSICS_OBJECTS_MAX_CAPACITY)
        {
            // Pull a voxel field from the pool
            size_t index = 0;
            if (numVFsCreated > 0)
                index = (voxelFieldIndices[numVFsCreated - 1] + 1) % PHYSICS_OBJECTS_MAX_CAPACITY;
            voxelFieldIndices[numVFsCreated] = index;

            VoxelFieldPhysicsData& vfpd = voxelFieldPool[index];
            numVFsCreated++;

            // Insert in the data
            vfpd.entityGuid = entityGuid;
            glm_mat4_copy(transform, vfpd.transform);
            vfpd.sizeX = sizeX;
            vfpd.sizeY = sizeY;
            vfpd.sizeZ = sizeZ;
            vfpd.voxelData = voxelData;
            vfpd.bodyId = JPH::BodyID();
            vfpd.simTransformId = registerSimulationTransform();

            return &vfpd;
        }
        else
        {
            std::cerr << "ERROR: voxel field creation has reached its limit" << std::endl;
            return nullptr;
        }
    }

    bool destroyVoxelField(VoxelFieldPhysicsData* vfpd)
    {
        for (size_t i = 0; i < numVFsCreated; i++)
        {
            size_t& index = voxelFieldIndices[i];
            if (&voxelFieldPool[index] == vfpd)
            {
                if (numVFsCreated > 1)
                {
                    // Overwrite the index with the back index,
                    // effectively deleting the index, while also
                    // preventing fragmentation.
                    index = voxelFieldIndices[numVFsCreated - 1];
                }
                numVFsCreated--;

                // Destroy voxel data.
                delete[] vfpd->voxelData;

                // Remove and delete the voxel field body.
                BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
                bodyInterface.RemoveBody(vfpd->bodyId);
                bodyInterface.DestroyBody(vfpd->bodyId);
                unregisterSimulationTransform(vfpd->simTransformId);

                return true;
            }
        }
        return false;
    }

    uint8_t getVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z)
    {
        if (x < 0 || y < 0 || z < 0 ||
            x >= vfpd.sizeX || y >= vfpd.sizeY || z >= vfpd.sizeZ)
            return 0;
        return vfpd.voxelData[(size_t)x * vfpd.sizeY * vfpd.sizeZ + (size_t)y * vfpd.sizeZ + (size_t)z];
    }

    bool setVoxelDataAtPosition(const VoxelFieldPhysicsData& vfpd, const int32_t& x, const int32_t& y, const int32_t& z, uint8_t data)
    {
        if (x < 0 || y < 0 || z < 0 ||
            x >= vfpd.sizeX || y >= vfpd.sizeY || z >= vfpd.sizeZ)
            return false;
        vfpd.voxelData[(size_t)x * vfpd.sizeY * vfpd.sizeZ + (size_t)y * vfpd.sizeZ + (size_t)z] = data;
        return true;
    }

    void expandVoxelFieldBounds(VoxelFieldPhysicsData& vfpd, ivec3 boundsMin, ivec3 boundsMax, ivec3& outOffset)
    {
        ivec3 newSize;
        glm_ivec3_maxv(ivec3{ (int32_t)vfpd.sizeX, (int32_t)vfpd.sizeY, (int32_t)vfpd.sizeZ }, ivec3{ boundsMax[0] + 1, boundsMax[1] + 1, boundsMax[2] + 1 }, newSize);

        glm_ivec3_minv(ivec3{ 0, 0, 0 }, boundsMin, outOffset);
        glm_ivec3_mul(outOffset, ivec3{ -1, -1, -1 }, outOffset);
        glm_ivec3_add(newSize, outOffset, newSize);  // Adds on the offset.

        // Create a new data grid with new bounds.
        size_t arraySize = newSize[0] * newSize[1] * newSize[2];
        uint8_t* newVD = new uint8_t[arraySize];
        for (size_t i = 0; i < arraySize; i++)
            newVD[i] = 0;  // Init the value to be empty.

        for (size_t i = 0; i < vfpd.sizeX; i++)
        for (size_t j = 0; j < vfpd.sizeY; j++)
        for (size_t k = 0; k < vfpd.sizeZ; k++)
        {
            uint8_t data = vfpd.voxelData[i * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + k];
            ivec3 newIJK;
            glm_ivec3_add(ivec3{ (int32_t)i, (int32_t)j, (int32_t)k }, outOffset, newIJK);
            newVD[newIJK[0] * newSize[1] * newSize[2] + newIJK[1] * newSize[2] + newIJK[2]] = data;
        }
        delete[] vfpd.voxelData;
        vfpd.voxelData = newVD;

        // Update size for voxel data structure.
        vfpd.sizeX = (size_t)newSize[0];
        vfpd.sizeY = (size_t)newSize[1];
        vfpd.sizeZ = (size_t)newSize[2];

        // Offset the transform.
        glm_translate(vfpd.transform, vec3{ -(float_t)outOffset[0], -(float_t)outOffset[1], -(float_t)outOffset[2] });
    }

    void shrinkVoxelFieldBoundsAuto(VoxelFieldPhysicsData& vfpd, ivec3& outOffset)
    {
        ivec3 boundsMin = { vfpd.sizeX, vfpd.sizeY, vfpd.sizeZ };
        ivec3 boundsMax = { 0, 0, 0 };
        for (size_t i = 0; i < vfpd.sizeX; i++)
        for (size_t j = 0; j < vfpd.sizeY; j++)
        for (size_t k = 0; k < vfpd.sizeZ; k++)
            if (getVoxelDataAtPosition(vfpd, i, j, k) != 0)
            {
                ivec3 ijk = { i, j, k };
                glm_ivec3_minv(boundsMin, ijk, boundsMin);
                glm_ivec3_maxv(boundsMax, ijk, boundsMax);
            }
        glm_ivec3_mul(boundsMin, ivec3{ -1, -1, -1 }, outOffset);

        // Set the new bounds to the smaller amount.
        ivec3 newSize;
        glm_ivec3_add(boundsMax, ivec3{ 1, 1, 1 }, newSize);
        glm_ivec3_sub(newSize, boundsMin, newSize);

        // @COPYPASTA
        // Create a new data grid with new bounds.
        size_t arraySize = newSize[0] * newSize[1] * newSize[2];
        uint8_t* newVD = new uint8_t[arraySize];
        for (size_t i = 0; i < arraySize; i++)
            newVD[i] = 0;  // Init the value to be empty.

        for (size_t i = 0; i < vfpd.sizeX; i++)
        for (size_t j = 0; j < vfpd.sizeY; j++)
        for (size_t k = 0; k < vfpd.sizeZ; k++)
        {
            uint8_t data = vfpd.voxelData[i * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + k];
            if (data == 0)
                continue;  // Skip empty spaces (to also prevent inserting into out of bounds if shrinking the array).

            ivec3 newIJK;
            glm_ivec3_add(ivec3{ (int32_t)i, (int32_t)j, (int32_t)k }, outOffset, newIJK);
            newVD[newIJK[0] * newSize[1] * newSize[2] + newIJK[1] * newSize[2] + newIJK[2]] = data;
        }
        delete[] vfpd.voxelData;
        vfpd.voxelData = newVD;

        // Update size for voxel data structure.
        vfpd.sizeX = (size_t)newSize[0];
        vfpd.sizeY = (size_t)newSize[1];
        vfpd.sizeZ = (size_t)newSize[2];

        // Offset the transform.
        glm_translate(vfpd.transform, vec3{ -(float_t)outOffset[0], -(float_t)outOffset[1], -(float_t)outOffset[2] });

        std::cout << "Shurnk to { " << vfpd.sizeX << ", " << vfpd.sizeY << ", " << vfpd.sizeZ << " }" << std::endl;
    }

    void cookVoxelDataIntoShape(VoxelFieldPhysicsData& vfpd, const std::string& entityGuid, std::vector<VoxelFieldCollisionShape>& outShapes)
    {
        BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

        // Recreate shape from scratch (property/feature of static compound shape).
        if (!vfpd.bodyId.IsInvalid())
        {
            bodyInterface.RemoveBody(vfpd.bodyId);
            bodyInterface.DestroyBody(vfpd.bodyId);
            vfpd.bodyId = BodyID();
        }

        // Create shape for each voxel.
        // (Simple greedy algorithm that pushes as far as possible in one dimension, then in another while throwing away portions that don't fit)
        // (Actually..... right now it's not a greedy algorithm and it's just a simple depth first flood that's good enough for now)  -Timo 2023/09/27
        Ref<StaticCompoundShapeSettings> compoundShape = new StaticCompoundShapeSettings;

        bool* processed = new bool[vfpd.sizeX * vfpd.sizeY * vfpd.sizeZ];  // Init processing datastructure
        for (size_t i = 0; i < vfpd.sizeX * vfpd.sizeY * vfpd.sizeZ; i++)
            processed[i] = false;

        for (size_t i = 0; i < vfpd.sizeX; i++)
        for (size_t j = 0; j < vfpd.sizeY; j++)
        for (size_t k = 0; k < vfpd.sizeZ; k++)
        {
            size_t idx = i * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + k;
            if (vfpd.voxelData[idx] == 0 || processed[idx])
                continue;
            
            // Start greedy search.
            if (vfpd.voxelData[idx] == 1)
            {
                uint8_t myType = vfpd.voxelData[idx];

                // Filled space search.
                size_t encX = 1,  // Encapsulation sizes. Multiply it all together to get the count of encapsulation.
                    encY = 1,
                    encZ = 1;
                for (size_t x = i + 1; x < vfpd.sizeX; x++)
                {
                    // Test whether next position is viable.
                    size_t idx = x * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + k;
                    bool viable = (vfpd.voxelData[idx] == myType && !processed[idx]);
                    if (!viable)
                        break;  // Exit if not viable.
                    
                    encX++; // March forward.
                }
                for (size_t y = j + 1; y < vfpd.sizeY; y++)
                {
                    // Test whether next row of positions are viable.
                    bool viable = true;
                    for (size_t x = i; x < i + encX; x++)
                    {
                        size_t idx = x * vfpd.sizeY * vfpd.sizeZ + y * vfpd.sizeZ + k;
                        viable &= (vfpd.voxelData[idx] == myType && !processed[idx]);
                        if (!viable)
                            break;
                    }

                    if (!viable)
                        break;  // Exit if not viable.
                    
                    encY++; // March forward.
                }
                for (size_t z = k + 1; z < vfpd.sizeZ; z++)
                {
                    // Test whether next sheet of positions are viable.
                    bool viable = true;
                    for (size_t x = i; x < i + encX; x++)
                    for (size_t y = j; y < j + encY; y++)
                    {
                        size_t idx = x * vfpd.sizeY * vfpd.sizeZ + y * vfpd.sizeZ + z;
                        viable &= (vfpd.voxelData[idx] == myType && !processed[idx]);
                        if (!viable)
                            break;
                    }

                    if (!viable)
                        break;  // Exit if not viable.
                    
                    encZ++; // March forward.
                }

                // Mark all claimed as processed.
                for (size_t x = 0; x < encX; x++)
                for (size_t y = 0; y < encY; y++)
                for (size_t z = 0; z < encZ; z++)
                {
                    size_t idx = (x + i) * vfpd.sizeY * vfpd.sizeZ + (y + j) * vfpd.sizeZ + (z + k);
                    processed[idx] = true;
                }

                // Create shape.
                Vec3 extent((float_t)encX * 0.5f, (float_t)encY * 0.5f, (float_t)encZ * 0.5f);
                Vec3 origin((float_t)i + extent.GetX(), (float_t)j + extent.GetY(), (float_t)k + extent.GetZ());
                Quat rotation = Quat::sIdentity();
                compoundShape->AddShape(origin, rotation, new BoxShape(extent));

                // Add shape props to `outShapes`.
                VoxelFieldCollisionShape vfcs = {};
                glm_vec3_copy(vec3{ origin.GetX(), origin.GetY(), origin.GetZ() }, vfcs.origin);
                glm_quat_copy(versor{ rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW() }, vfcs.rotation);
                glm_vec3_copy(vec3{ extent.GetX(), extent.GetY(), extent.GetZ() }, vfcs.extent);
                outShapes.push_back(vfcs);
            }
            else if (vfpd.voxelData[idx] >= 2)
            {
                uint8_t myType = vfpd.voxelData[idx];
                bool even = (myType == 2 || myType == 4);

                // Slope space search.
                size_t length = 1;   // Amount slope takes to go down 1 level.
                size_t width = 1;    // # spaces wide the same slope pattern goes.
                size_t repeats = 1;  // # times this pattern repeats downward.

                // Get length dimension.
                for (size_t l = (even ? k : i) + 1; l < (even ? vfpd.sizeZ : vfpd.sizeX); l++)
                {
                    // Test whether next position is viable.
                    size_t idx;
                    if (even)
                        idx = i * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + l;
                    else
                        idx = l * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + k;

                    bool viable = (vfpd.voxelData[idx] == myType && !processed[idx]);
                    if (!viable)
                        break;  // Exit if not viable.
                    
                    length++; // March forward.
                }

                // Get width dimension.
                for (size_t w = (even ? i : k) + 1; w < (even ? vfpd.sizeX : vfpd.sizeZ); w++)
                {
                    // Test whether next row of positions are viable.
                    bool viable = true;
                    for (size_t l = (even ? k : i); l < (even ? k : i) + length; l++)
                    {
                        size_t idx;
                        if (even)
                            idx = w * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + l;
                        else
                            idx = l * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + w;

                        viable &= (vfpd.voxelData[idx] == myType && !processed[idx]);
                        if (!viable)
                            break;
                    }

                    if (!viable)
                        break;  // Exit if not viable.
                    
                    width++; // March forward.
                }

                // @TODO: get repeats.

                // Mark all claimed as processed.
                for (size_t x = 0; x < (even ? width : length); x++)
                for (size_t z = 0; z < (even ? length : width); z++)
                {
                    size_t idx = (x + i) * vfpd.sizeY * vfpd.sizeZ + j * vfpd.sizeZ + (z + k);
                    processed[idx] = true;
                }

                // Create shape.
                float_t realLength = std::sqrtf(1.0f + (float_t)length * (float_t)length);
                float_t angle      = std::asinf(1.0f / realLength);
                float_t realHeight = std::sinf(90.0f - angle);

                float_t yoff = 0.0f;
                Quat rotation;
                if (myType == 2)
                    rotation = Quat::sEulerAngles(Vec3(-angle, 0.0f, 0.0f));
                else if (myType == 3)
                    rotation = Quat::sEulerAngles(Vec3(0.0f, 0.0f, angle));
                else if (myType == 4)
                {
                    rotation = Quat::sEulerAngles(Vec3(angle, 0.0f, 0.0f));
                    yoff = 1.0f;
                }
                else if (myType == 5)
                {
                    rotation = Quat::sEulerAngles(Vec3(0.0f, 0.0f, -angle));
                    yoff = 1.0f;
                }
                else
                    std::cerr << "[COOKING VOXEL SHAPES]" << std::endl
                        << "WARNING: voxel type " << myType << " was not recognized." << std::endl;
                
                Vec3 extent((float_t)(even ? width : realLength) * 0.5f, (float_t)realHeight * 0.5f, (float_t)(even ? realLength : width) * 0.5f);

                Vec3 origin = Vec3{ (float_t)i, (float_t)j + yoff, (float_t)k } + rotation * (extent + Vec3(0.0f, -realHeight, 0.0f));

                compoundShape->AddShape(origin, rotation, new BoxShape(extent));

                // Add shape props to `outShapes`.
                // @COPYPASTA
                VoxelFieldCollisionShape vfcs = {};
                glm_vec3_copy(vec3{ origin.GetX(), origin.GetY(), origin.GetZ() }, vfcs.origin);
                glm_quat_copy(versor{ rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW() }, vfcs.rotation);
                glm_vec3_copy(vec3{ extent.GetX(), extent.GetY(), extent.GetZ() }, vfcs.extent);
                outShapes.push_back(vfcs);
            }
        }

        if (compoundShape->mSubShapes.size() == 0)
            return;  // Cannot create empty body.

        // Create body.
        vec4 pos;
        mat4 rot;
        vec3 sca;
        glm_decompose(vfpd.transform, pos, rot, sca);
        versor rotV;
        glm_mat4_quat(rot, rotV);

        // DYNAMIC is set so that voxel field can move around with the influence of other physics objects.
        vfpd.bodyId = bodyInterface.CreateBody(BodyCreationSettings(compoundShape, RVec3(pos[0], pos[1], pos[2]), Quat(rotV[0], rotV[1], rotV[2], rotV[3]), EMotionType::Dynamic, Layers::MOVING))->GetID();
        bodyInterface.SetGravityFactor(vfpd.bodyId, 0.0f);
        bodyInterface.AddBody(vfpd.bodyId, EActivation::DontActivate);

        // Add guid into references.
        bodyIdToEntityGuidMap[vfpd.bodyId] = entityGuid;
    }

    void setVoxelFieldBodyTransform(VoxelFieldPhysicsData& vfpd, vec3 newPosition, versor newRotation)
    {
        BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

        RVec3 newPositionReal(newPosition[0], newPosition[1], newPosition[2]);
        Quat newRotationJolt(newRotation[0], newRotation[1], newRotation[2], newRotation[3]);

        EActivation activation = EActivation::DontActivate;
        if (bodyInterface.GetMotionType(vfpd.bodyId) == EMotionType::Dynamic)
            activation = EActivation::Activate;
        bodyInterface.SetPositionAndRotation(vfpd.bodyId, newPositionReal, newRotationJolt, activation);
    }

    void moveVoxelFieldBodyKinematic(VoxelFieldPhysicsData& vfpd, vec3 newPosition, versor newRotation, float_t simDeltaTime)
    {
        RVec3 newPositionReal(newPosition[0], newPosition[1], newPosition[2]);
        Quat newRotationJolt(newRotation[0], newRotation[1], newRotation[2], newRotation[3]);
        physicsSystem->GetBodyInterface().MoveKinematic(vfpd.bodyId, newPositionReal, newRotationJolt, simDeltaTime);
    }

    void setVoxelFieldBodyKinematic(VoxelFieldPhysicsData& vfpd, bool isKinematic)
    {
        physicsSystem->GetBodyInterface().SetMotionType(vfpd.bodyId, (isKinematic ? EMotionType::Kinematic : EMotionType::Dynamic), EActivation::DontActivate);
    }

    //
    // Capsule pool
    //
    CapsulePhysicsData capsulePool[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t capsuleIndices[PHYSICS_OBJECTS_MAX_CAPACITY];
    size_t numCapsCreated = 0;

    CapsulePhysicsData* createCharacter(const std::string& entityGuid, vec3 position, const float_t& radius, const float_t& height, bool enableCCD)
    {
        if (numCapsCreated < PHYSICS_OBJECTS_MAX_CAPACITY)
        {
            // Pull a capsule from the pool
            size_t index = 0;
            if (numCapsCreated > 0)
                index = (capsuleIndices[numCapsCreated - 1] + 1) % PHYSICS_OBJECTS_MAX_CAPACITY;
            capsuleIndices[numCapsCreated] = index;

            CapsulePhysicsData& cpd = capsulePool[index];
            numCapsCreated++;

            // Insert in the data
            cpd.entityGuid = entityGuid;
            glm_vec3_copy(position, cpd.currentCOMPosition);
            glm_vec3_copy(position, cpd.prevCOMPosition);
            cpd.radius = radius;
            cpd.height = height;

            // Create physics cylinder.
            ShapeRefC cylinderShape = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * height + radius, 0), Quat::sIdentity(), new CylinderShape(0.5f * height + radius, radius)).Create().Get();

            Ref<CharacterSettings> settings = new CharacterSettings;
            settings->mMaxSlopeAngle = glm_rad(46.0f);
            settings->mLayer = Layers::MOVING;
            settings->mShape = cylinderShape;
            settings->mGravityFactor = 0.0f;  // @NOTE: this is for the collide and slide algorithm. It works having grav factor be 0.  -Timo 2024/01/28 (EDIT: 2024/02/10)

            // @NOTE: this was in the past 0.0f, but after introducing the slightest slope, the character starts sliding down.
            //        This gives everything a bit of a tacky feel, but I feel like that makes the physics for the characters
            //        feel real (gives character lol). Plus, the characters can hold up to a rotating moving platform.  -Timo 2023/09/30
            // @AMEND: The friction level is set to 0.0f, bc 0.5f 
            settings->mFriction = 0.0f;

            settings->mSupportingVolume = Plane(Vec3::sAxisY(), -(0.5f * height + (1.0f - std::sinf(settings->mMaxSlopeAngle))));
            cpd.character = new JPH::Character(settings, RVec3(position[0], position[1], position[2]), Quat::sIdentity(), (int64_t)UserDataMeaning::IS_CHARACTER, physicsSystem);
            if (enableCCD)
                physicsSystem->GetBodyInterface().SetMotionQuality(cpd.character->GetBodyID(), EMotionQuality::LinearCast);

            cpd.character->AddToPhysicsSystem(EActivation::Activate);
            cpd.simTransformId = registerSimulationTransform();

            // Add guid into references.
            bodyIdToEntityGuidMap[cpd.character->GetBodyID()] = entityGuid;

            return &cpd;
        }
        else
        {
            std::cerr << "ERROR: capsule creation has reached its limit" << std::endl;
            HAWSOO_CRASH();
            return nullptr;
        }
    }

    bool destroyCapsule(CapsulePhysicsData* cpd)
    {
        for (size_t i = 0; i < numCapsCreated; i++)
        {
            size_t& index = capsuleIndices[i];
            if (&capsulePool[index] == cpd)
            {
                if (numCapsCreated > 1)
                {
                    // Overwrite the index with the back index,
                    // effectively deleting the index
                    index = capsuleIndices[numCapsCreated - 1];
                }
                numCapsCreated--;

                // Remove and delete the physics capsule.
                cpd->character->RemoveFromPhysicsSystem();
                unregisterSimulationTransform(cpd->simTransformId);

                return true;
            }
        }
        return false;
    }

    size_t getNumCapsules()
    {
        return numCapsCreated;
    }

    CapsulePhysicsData* getCapsuleByIndex(size_t index)
    {
        return &capsulePool[capsuleIndices[index]];
    }

    float_t getLengthOffsetToBase(const CapsulePhysicsData& cpd)
    {
        return cpd.height * 0.5f + cpd.radius - collisionTolerance * 0.5f;
    }

    void setCharacterPosition(CapsulePhysicsData& cpd, vec3 position)
    {
        cpd.character->SetPosition(RVec3(position[0], position[1] - collisionTolerance * 0.5f, position[2]));
    }

    void getCharacterPosition(const CapsulePhysicsData& cpd, vec3& outPosition)
    {
        RVec3 pos = cpd.character->GetCenterOfMassPosition();
        outPosition[0] = pos.GetX();
        outPosition[1] = pos.GetY();
        outPosition[2] = pos.GetZ();
    }

    void moveCharacter(CapsulePhysicsData& cpd, vec3 velocity)
    {
        cpd.character->SetLinearVelocity(Vec3(velocity[0], velocity[1], velocity[2]));
    }

    void setGravityFactor(CapsulePhysicsData& cpd, float_t newGravityFactor)
    {
        BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
        bodyInterface.SetGravityFactor(cpd.character->GetBodyID(), newGravityFactor);
    }

    void getLinearVelocity(const CapsulePhysicsData& cpd, vec3& outVelocity)
    {
        Vec3 velo = cpd.character->GetLinearVelocity();
        outVelocity[0] = velo.GetX();
        outVelocity[1] = velo.GetY();
        outVelocity[2] = velo.GetZ();
    }

    bool isGrounded(const CapsulePhysicsData& cpd)
    {
        return (cpd.character->GetGroundState() == CharacterBase::EGroundState::OnGround);
    }

    bool isSlopeTooSteepForCharacter(const CapsulePhysicsData& cpd, Vec3Arg normal)
    {
        return cpd.character->IsSlopeTooSteep(normal);
    }

    void transformSwap()
    {
        ZoneScoped;

        simSetOffset++;
    }

    void copyResultTransforms()
    {
        ZoneScoped;

        auto& bodyInterface = physicsSystem->GetBodyInterface();

        // Set current transform.
        for (size_t i = 0; i < numVFsCreated; i++)
        {
            VoxelFieldPhysicsData& vfpd = voxelFieldPool[voxelFieldIndices[i]];
            // vfpd.COMPositionDifferent = false;  // @TODO: implement this!

            if (vfpd.bodyId.IsInvalid()/* || !bodyInterface.IsActive(vfpd.bodyId)*/)
                continue;

            RVec3 pos;
            Quat rot;
            bodyInterface.GetPositionAndRotation(vfpd.bodyId, pos, rot);
            updateSimulationTransformPosition(vfpd.simTransformId, vec3{ pos.GetX(), pos.GetY(), pos.GetZ() });
            updateSimulationTransformRotation(vfpd.simTransformId, versor{ rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW() });
        }
        for (size_t i = 0; i < numCapsCreated; i++)
        {
            CapsulePhysicsData& cpd = capsulePool[capsuleIndices[i]];
            cpd.COMPositionDifferent = false;

            if (cpd.character == nullptr)
                continue;

            cpd.character->PostSimulation(collisionTolerance);

            RVec3 pos = cpd.character->GetCenterOfMassPosition();  // @NOTE: I thought that `GetPosition` would be quicker/lighter than `GetCenterOfMassPosition`, but getting the position negates the center of mass, thus causing an extra subtract operation.
            updateSimulationTransformPosition(cpd.simTransformId, vec3{ pos.GetX(), pos.GetY(), pos.GetZ() });

            cpd.COMPositionDifferent = (glm_vec3_distance2(cpd.currentCOMPosition, cpd.prevCOMPosition) > 0.000001f);
        }
    }

    inline float_t getPhysicsAlpha()
    {
        return (SDL_GetTicks64() - lastTick) * oneOverPhysicsDeltaTimeInMS * globalState::timescale;
    }

    void recalcInterpolatedTransformsSet()
    {
        ZoneScoped;

        size_t simSetOffsetCopy = simSetOffset;

        float_t physicsAlpha = getPhysicsAlpha();
        if (physicsAlpha > 1.0f)
        {
            // Move to next set of transforms (it hopefully is added into next set of transforms).
            // However, the tick that moves to the next set of transforms is handled on the simulation
            // thread at the beginning of a loop after a delay to line up the 40hz simulation speed.
            // That means that it's very much possible to get off on the render thread, so edit the
            // accessing set of transforms here if needed.
            simSetOffsetCopy++;
            physicsAlpha -= 1.0f;
        }

        size_t prevSimSet = (simSetOffsetCopy + 0) % 3;
        size_t currentSimSet = (simSetOffsetCopy + 1) % 3;

        for (size_t i = 0; i < registeredSimSetIndices.size(); i++)
        {
            size_t index = registeredSimSetIndices[i];
            glm_vec3_lerp(
                simSetChain[prevSimSet]->simTransforms[index].position,
                simSetChain[currentSimSet]->simTransforms[index].position,
                physicsAlpha,
                calcInterpolatedSet->simTransforms[index].position
            );
            glm_quat_nlerp(
                simSetChain[prevSimSet]->simTransforms[index].rotation,
                simSetChain[currentSimSet]->simTransforms[index].rotation,
                physicsAlpha,
                calcInterpolatedSet->simTransforms[index].rotation
            );
        }
    }

    void setWorldGravity(vec3 newGravity)
    {
        physicsSystem->SetGravity(Vec3(newGravity[0], newGravity[1], newGravity[2]));
    }

    void getWorldGravity(vec3& outGravity)
    {
        Vec3 grav = physicsSystem->GetGravity();
        outGravity[0] = grav.GetX();
        outGravity[1] = grav.GetY();
        outGravity[2] = grav.GetZ();
    }

    size_t getCollisionLayer(const std::string& layerName)
    {
        return 0;  // @INCOMPLETE: for now, just ignore the collision layers and check everything.
    }

    bool INTERNALRaycastFunction(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid, float_t& outFraction, bool collectSurfNormal, vec3& outSurfaceNormal)
    {
        ZoneScoped;

#ifdef _DEVELOP
        if (engine->generateCollisionDebugVisualization)
        {
            vec3 pt2;
            glm_vec3_add(origin, directionAndMagnitude, pt2);
            drawDebugVisLine(origin, pt2);
        }
#endif

        RRayCast ray{
            Vec3(origin[0], origin[1], origin[2]),
            Vec3(directionAndMagnitude[0], directionAndMagnitude[1], directionAndMagnitude[2])
        };
        RayCastResult result;
        if (physicsSystem->GetNarrowPhaseQuery().CastRay(ray, result, SpecifiedBroadPhaseLayerFilter(BroadPhaseLayers::MOVING), SpecifiedObjectLayerFilter(Layers::MOVING)))
        {
            outFraction = result.GetEarlyOutFraction();

            if (bodyIdToEntityGuidMap.find(result.mBodyID) == bodyIdToEntityGuidMap.end())
            {
                std::cout << "[RAYCAST]" << std::endl
                    << "WARNING: body ID " << result.mBodyID.GetIndexAndSequenceNumber() << " didn\'t match any entity GUIDs." << std::endl;
            }
            else
            {
                outHitGuid = bodyIdToEntityGuidMap[result.mBodyID];
            }

            if (collectSurfNormal)
            {
                // @CHECK: for some reason surface normal is the direction INTO the shape??? Seems sus/wrong.  -Timo 2024/01/11
                // @REPLY: Ummmm, so for some things it's the correct normal and sometimes it's not???  -Timo 2024/01/13
                // @REPLY: so getting the shape space surface normal was what was happening. Using transformed shape, now we can use `GetWorldSpaceSurfaceNormal`, which is the way I was using the previous function. This should do it.  -Timo 2024/01/13
                // @NOCHECKIN: the test code below:
                // physicsSystem->GetBodyInterface().GetTransformedShape(result.mBodyID).CastShape
                Vec3 surfNormal = physicsSystem->GetBodyInterface().GetTransformedShape(result.mBodyID).GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(outFraction));
                outSurfaceNormal[0] = surfNormal.GetX();
                outSurfaceNormal[1] = surfNormal.GetY();
                outSurfaceNormal[2] = surfNormal.GetZ();
            }

            return true;
        }
        return false;
    }

    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid, float_t& outFraction, vec3& outSurfaceNormal)
    {
        return INTERNALRaycastFunction(origin, directionAndMagnitude, outHitGuid, outFraction, true, outSurfaceNormal);
    }

    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid, float_t& outFraction)
    {
        vec3 _;
        return INTERNALRaycastFunction(origin, directionAndMagnitude, outHitGuid, outFraction, false, _);
    }

    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid)
    {
        float_t _;
        vec3 __;
        return INTERNALRaycastFunction(origin, directionAndMagnitude, outHitGuid, _, false, __);
    }

    bool cylinderCast(vec3 origin, float_t radius, float_t height, JPH::BodyID ignoreBodyId, vec3 directionAndMagnitude, float_t& outFraction, vec3& outSurfaceNormal)
    {
        vec3 _;
        return cylinderCast(origin, radius, height, ignoreBodyId, directionAndMagnitude, outFraction, outSurfaceNormal, _);
    }

    bool cylinderCast(vec3 origin, float_t radius, float_t height, JPH::BodyID ignoreBodyId, vec3 directionAndMagnitude, float_t& outFraction, vec3& outSurfaceNormal, vec3& outLocalContactPosition)
    {
        RShapeCast sc(
            new CylinderShape(height * 0.5f + radius, radius),
            Vec3(1.0f, 1.0f, 1.0f),
            Mat44::sTranslation(Vec3(origin[0], origin[1], origin[2])),
            Vec3(directionAndMagnitude[0], directionAndMagnitude[1], directionAndMagnitude[2])
        );
        ShapeCastSettings scs;

        class MyCollector : public CastShapeCollector
        {
        public:
            MyCollector(PhysicsSystem &inPhysicsSystem, JPH::BodyID ignoreBodyId) :
                mPhysicsSystem(inPhysicsSystem),
                mIgnoreBodyId(ignoreBodyId) { }

            virtual void AddHit(const ShapeCastResult &inResult) override
            {
                // Test if this collision is closer than the previous one
                if (inResult.mFraction < GetEarlyOutFraction())
                {
                    if (inResult.mBodyID2 == mIgnoreBodyId)
                        return;

                    // Lock the body
                    BodyLockRead lock(mPhysicsSystem.GetBodyLockInterfaceNoLock(), inResult.mBodyID2);
                    JPH_ASSERT(lock.Succeeded());  // When this runs all bodies are locked so this should not fail
                    const Body *body = &lock.GetBody();

                    if (body->IsSensor())
                        return;

                    // Update early out fraction to this hit
                    UpdateEarlyOutFraction(inResult.mFraction);

                    // Get the contact properties
                    mBody = body;
                    mSubShapeID2 = inResult.mSubShapeID2;
                    mLocalContactPosition = inResult.mContactPointOn2;
                    mContactNormal = -inResult.mPenetrationAxis.Normalized();
                }
            }

            // Configuration
            PhysicsSystem&      mPhysicsSystem;
            JPH::BodyID         mIgnoreBodyId;

            // Resulting closest collision
            const Body*        mBody = nullptr;
            SubShapeID         mSubShapeID2;
            RVec3              mLocalContactPosition;
            Vec3               mContactNormal;
        };
        MyCollector collector(*physicsSystem, ignoreBodyId);

        physicsSystem->GetNarrowPhaseQuery().CastShape(
            sc,
            scs,
            Vec3(origin[0], origin[1], origin[2]),
            collector,
            DefaultBroadPhaseLayerFilter(ObjectVsBroadPhaseLayerFilterImpl(), Layers::MOVING),
            DefaultObjectLayerFilter(ObjectLayerPairFilterImpl(), Layers::MOVING)
        );
        if (collector.mBody != nullptr)
        {
            outFraction = collector.GetEarlyOutFraction();
            outSurfaceNormal[0] = collector.mContactNormal.GetX();
            outSurfaceNormal[1] = collector.mContactNormal.GetY();
            outSurfaceNormal[2] = collector.mContactNormal.GetZ();
            outLocalContactPosition[0] = collector.mLocalContactPosition.GetX();
            outLocalContactPosition[1] = collector.mLocalContactPosition.GetY();
            outLocalContactPosition[2] = collector.mLocalContactPosition.GetZ();
            return true;
        }

        return false;
    }

    bool capsuleOverlap(vec3 origin, versor rotation, float_t radius, float_t height, JPH::BodyID ignoreBodyId, std::vector<BodyAndSubshapeID>& outHitIds)
    {
        bool hitAdded = false;

        RMat44 transform =
            RMat44::sRotationTranslation(
                Quat(rotation[0], rotation[1], rotation[2], rotation[3]),
                Vec3(origin[0], origin[1], origin[2])
            );
        CollideShapeSettings css;  // @NOTE: may need to increase things to collide with to include inactive edges too?

        class MyCollector : public CollideShapeCollector
        {
        public:
            MyCollector(PhysicsSystem &inPhysicsSystem, JPH::BodyID ignoreBodyId, std::vector<BodyAndSubshapeID>& outHitIds, bool& hitAdded) :
                mPhysicsSystem(inPhysicsSystem),
                mIgnoreBodyId(ignoreBodyId),
                mHitIds(outHitIds),
                mHitAdded(hitAdded) { }

            virtual void AddHit(const CollideShapeResult &inResult) override
            {
                if (inResult.mBodyID2 == mIgnoreBodyId)
                    return;

                // Lock the body.
                BodyLockRead lock(mPhysicsSystem.GetBodyLockInterfaceNoLock(), inResult.mBodyID2);
                JPH_ASSERT(lock.Succeeded());  // When this runs all bodies are locked so this should not fail
                const Body *body = &lock.GetBody();

                if (body->IsSensor())
                    return;

                // Push the collision.
                mHitIds.push_back({
                    .bodyId = inResult.mBodyID2,
                    .subShapeId = inResult.mSubShapeID2,
                    .hitPosition = {
                        inResult.mContactPointOn2.GetX(),
                        inResult.mContactPointOn2.GetY(),
                        inResult.mContactPointOn2.GetZ(),
                    },
                });
            }

            // Configuration
            PhysicsSystem&                  mPhysicsSystem;
            JPH::BodyID                     mIgnoreBodyId;

            // Resulting closest collision
            std::vector<BodyAndSubshapeID>& mHitIds;
            bool&                           mHitAdded;
        };
        MyCollector collector(*physicsSystem, ignoreBodyId, outHitIds, hitAdded);

        physicsSystem->GetNarrowPhaseQuery()
            .CollideShape(
                new CapsuleShape(height * 0.5f, radius),  // @RESEARCH: I STILL don't know whether this causes memory leaks by not deleting.
                Vec3(1.0f, 1.0f, 1.0f),
                transform,
                css,
                Vec3::sZero(),
                collector,
                SpecifiedBroadPhaseLayerFilter(BroadPhaseLayers::HIT_HURT_BOX),  // @RESEARCH: check that this actually reacts to collisions with only the hit/hurt boxes!
                SpecifiedObjectLayerFilter(Layers::HIT_HURT_BOX)                 // @TODO: make a kinematic mutatable compound shape that can be in this layer and automatically update to the animation bones the shapes are assigned to.
            );

        return hitAdded;
    }

    // Skeleton-bound hit capsule set.

    // @TODO: make all the pool stuff into its own interface and `Pool` thingo.
    class SkeletonBoundHitCapsuleSetsPool
    {
    public:
        SkeletonBoundHitCapsuleSetsPool()
            : _pool(new SBHCSWithIncrement[(size_t)kPoolSize])
            , _poolElemCount(0)
        {
            for (size_t i = 0; i < kPoolSize; i++)
                _pool[i] = {};
        }

        ~SkeletonBoundHitCapsuleSetsPool()
        {
            delete[] _pool;
        }

        sbhcs_key_t allocNewSBHCS()
        {
            // Find open spot to allocate.
            size_t allocIdx = (size_t)-1;
            for (size_t i = 0; i < kPoolSize; i++)
                if (_pool[i].metadata & kDeletedMask)
                {
                    allocIdx = i;
                    break;
                }
            if (allocIdx == (size_t)-1)
            {
                std::cerr << "ERROR: new SkeletonBoundHitCapsuleSet not able to be allocated. No more space"
                    << std::endl;
                HAWSOO_CRASH();
            }
            auto& spot = _pool[allocIdx];
            
            // Set spot to "undeleted" and init.
            spot.metadata = (spot.metadata & ~kDeletedMask);
            spot.sbhcs = {};

            // Generate key.
            assert(allocIdx < (size_t)kPoolSize);  // Idk how you got this assert to trip if you did.
            sbhcs_key_t newKey = 0;
            newKey = ((sbhcs_key_t)spot.metadata << 16) | (sbhcs_key_t)allocIdx;

            _poolElemCount++;

            return newKey;
        }

        SkeletonBoundHitCapsuleSet* getSBHCSFromKey(sbhcs_key_t key)
        {
            if (isDeleted(key))
                return nullptr;
            return &getSBHCSWithIncrement(key)->sbhcs;
        }

        bool deleteSBHCSFromKey(sbhcs_key_t key)
        {
            if (isDeleted(key))
            {
                std::cerr << "ERROR: Attempted to delete already deleted key: "
                    << key << std::endl;
                return false;
            }

            // Increment and set to "deleted".
            auto sbhcswi = getSBHCSWithIncrement(key);
            uint16_t newMetadata = (sbhcswi->metadata & kCountMask);
            newMetadata++;
            newMetadata = (newMetadata | kDeletedMask);
            sbhcswi->metadata = newMetadata;

            _poolElemCount--;

            return true;
        }

    private:
        const static uint16_t kPoolSize    = 0xFFFF;
        const static uint16_t kDeletedMask = 0x8000;
        const static uint16_t kCountMask   = 0x7FFF;

        struct SBHCSWithIncrement
        {
            uint16_t metadata = 0x8000;  // Set to initially deleted and count to 0.
            SkeletonBoundHitCapsuleSet sbhcs;
        };
        SBHCSWithIncrement* _pool;
        size_t _poolElemCount;
        
        inline uint16_t getMetadataBits(sbhcs_key_t key)                  { return (uint16_t)(key >> 16); }
        inline uint16_t getIndexBits(sbhcs_key_t key)                     { return (uint16_t)key; }
        inline SBHCSWithIncrement* getSBHCSWithIncrement(sbhcs_key_t key) { return &_pool[(size_t)getIndexBits(key)]; }
        inline bool isDeleted(sbhcs_key_t key)
        {
            // Key indicates deleted.
            uint16_t metadata = getMetadataBits(key);
            if (metadata & kDeletedMask)
                return true;

            // Currently deleted.
            auto sbhcswi = getSBHCSWithIncrement(key);
            if (sbhcswi->metadata & kDeletedMask)
                return true;

            // Key increment doesn't equal current increment.
            if (sbhcswi->metadata & kCountMask != metadata & kCountMask)
                return true;
            
            // Not deleted.
            return false;
        }

        friend void renderDebugVisualization(VkCommandBuffer cmd);
    } sbhcsPool;

    sbhcs_key_t createSkeletonBoundHitCapsuleSet(std::vector<BoundHitCapsule>& hitCapsules, size_t simTransformId, vkglTF::Animator* skeleton)
    {
        ZoneScoped;

        sbhcs_key_t key = sbhcsPool.allocNewSBHCS();
        if (auto newSkeletonBoundHitCapsuleSet = sbhcsPool.getSBHCSFromKey(key))
        {
            newSkeletonBoundHitCapsuleSet->simTransformId = simTransformId;
            newSkeletonBoundHitCapsuleSet->skeleton = skeleton;

            // Add parts to mutable compound shape.
            Ref<MutableCompoundShapeSettings> compoundShape = new MutableCompoundShapeSettings;
            for (auto& hitCapsule : hitCapsules)
            {
                // Copy capsule params.
                SkeletonBoundHitCapsuleSet::BoundHitCapsuleSubShape newBHCSS = {};
                newBHCSS.boneName = hitCapsule.boneName;
                glm_vec3_copy(hitCapsule.offset, newBHCSS.offset);
                newBHCSS.height = hitCapsule.height;
                newBHCSS.radius = hitCapsule.radius;

                // Calc new shape connection to joint.
                mat4 jointMat;
                newSkeletonBoundHitCapsuleSet->skeleton->getJointMatrix(newBHCSS.boneName, jointMat);

                vec3 capsuleOrigin;
                glm_mat4_mulv3(jointMat, newBHCSS.offset, 1.0f, capsuleOrigin);

                versor jointRot;
                glm_mat4_quat(jointMat, jointRot);

                // Add new shape.
                compoundShape->AddShape(
                    Vec3(capsuleOrigin[0], capsuleOrigin[1], capsuleOrigin[2]),
                    Quat(jointRot[0], jointRot[1], jointRot[2], jointRot[3]),
                    new CapsuleShape(newBHCSS.height * 0.5f, newBHCSS.radius)
                );
                // @TODO: @NOCHECKIN: @INCOMPLETE: it looks like I don't get to assign a subshape id,
                //                                 so have to just rely on the physics engine itself.

                newSkeletonBoundHitCapsuleSet->hitCapsuleSubShapes.push_back(newBHCSS);
            }

            // Get joined body transform.
            auto& bodyInterface = physicsSystem->GetBodyInterface();

            RVec3 position(0.0f, 0.0f, 0.0f);
            Quat rotation = Quat::sIdentity();
            if (newSkeletonBoundHitCapsuleSet->simTransformId != (size_t)-1)
            {
                vec3 posGlm;
                versor rotGlm;
                getCurrentSimulationTransformPositionAndRotation(
                    newSkeletonBoundHitCapsuleSet->simTransformId,
                    posGlm,
                    rotGlm
                );
                position = RVec3(posGlm[0], posGlm[1], posGlm[2]);
                rotation = Quat(rotGlm[0], rotGlm[1], rotGlm[2], rotGlm[3]);
            }

            // Create kinematic body.
            Body& body =
                *bodyInterface.CreateBody(
                    BodyCreationSettings(
                        compoundShape,
                        position,
                        rotation,
                        EMotionType::Kinematic,
                        Layers::HIT_HURT_BOX
                    )
                );
            bodyInterface.AddBody(body.GetID(), EActivation::Activate);
            newSkeletonBoundHitCapsuleSet->bodyId = body.GetID();

            return key;
        }
        else
        {
            std::cerr << "ERROR: unable to get newly allocated `SkeletonBoundHitCapsuleSet` with key: " << key << std::endl;
            return kInvalidSBHCSKey;
        }
    }

    bool destroySkeletonBoundHitCapsuleSet(sbhcs_key_t key)
    {
        ZoneScoped;

        if (auto sbhcs = sbhcsPool.getSBHCSFromKey(key))
        {
            auto& bodyInterface = physicsSystem->GetBodyInterface();
            bodyInterface.DestroyBody(sbhcs->bodyId);

            return sbhcsPool.deleteSBHCSFromKey(key);
        }
        else
        {
            std::cerr << "ERROR: unable to find `SkeletonBoundHitCapsuleSet` with key: " << key << std::endl;
            return false;
        }
    }
    
    void updateSkeletonBoundHitCapsuleSet(sbhcs_key_t key)
    {
        ZoneScoped;

        if (auto sbhcs = sbhcsPool.getSBHCSFromKey(key))
        {
            {
                ZoneScopedN("Lock body to mutate subshapes");

                BodyInterface& noLock = physicsSystem->GetBodyInterfaceNoLock();
                BodyLockWrite lock(physicsSystem->GetBodyLockInterface(), sbhcs->bodyId);
                if (lock.Succeeded())
                {
                    Body& body = lock.GetBody();

                    Vec3 prevCom = body.GetCenterOfMassPosition();

                    // Get shape.
                    MutableCompoundShape *shape = static_cast<MutableCompoundShape *>(const_cast<Shape *>(body.GetShape()));

                    uint32_t count = shape->GetNumSubShapes();
                    for (uint32_t i = 0; i < count; i++)
                    {
                        auto& subShape = shape->GetSubShape(i);
                        auto& hitCapsuleSubShape = sbhcs->hitCapsuleSubShapes[i];

                        // Calc new transform for sub shape.
                        mat4 jointMat;
                        sbhcs->skeleton->getJointMatrix(hitCapsuleSubShape.boneName, jointMat);

                        vec3 capsuleOrigin;
                        glm_mat4_mulv3(jointMat, hitCapsuleSubShape.offset, 1.0f, capsuleOrigin);

                        versor jointRot;
                        glm_mat4_quat(jointMat, jointRot);

                        {
                            ZoneScopedN("Mutate subshape");

                            shape->ModifyShape(
                                i,
                                Vec3(capsuleOrigin[0], capsuleOrigin[1], capsuleOrigin[2]),
                                Quat(jointRot[0], jointRot[1], jointRot[2], jointRot[3])
                            );
                        }
                    }

                    // Notify body to reincorporate new shapes.
                    noLock.NotifyShapeChanged(sbhcs->bodyId, prevCom, false, EActivation::Activate);
                }
            }

            {
                ZoneScopedN("Apply joined body transform");

                if (sbhcs->simTransformId != (size_t)-1)
                {
                    vec3 posGlm;
                    versor rotGlm;
                    getCurrentSimulationTransformPositionAndRotation(
                        sbhcs->simTransformId,
                        posGlm,
                        rotGlm
                    );
                    physicsSystem->GetBodyInterface().SetPositionAndRotation(
                        sbhcs->bodyId,
                        RVec3(posGlm[0], posGlm[1], posGlm[2]),
                        Quat(rotGlm[0], rotGlm[1], rotGlm[2], rotGlm[3]),
                        EActivation::DontActivate  // Due to being a kinematic body.
                    );
                }
            }
        }
    }

    std::string bodyIdToEntityGuid(JPH::BodyID bodyId)
    {
        return bodyIdToEntityGuidMap[bodyId];
    }

#ifdef _DEVELOP
    void drawDebugVisLine(vec3 pt1, vec3 pt2, DebugVisLineType type)
    {
        DebugVisLine dvl = {};
        glm_vec3_copy(pt1, dvl.pt1);
        glm_vec3_copy(pt2, dvl.pt2);
        dvl.type = type;

        std::lock_guard<std::mutex> lg(mutateDebugVisLines);
        debugVisLines.push_back(dvl);
    }
    
    void drawDebugVisPoint(vec3 pt, DebugVisLineType type)
    {
        const static float_t radius = 0.25f;

        // Draw lines in each dimension.
        for (size_t dim = 0; dim < 3; dim++)
        {
            vec3 pt1;
            vec3 pt2;
            glm_vec3_copy(pt, pt1);
            glm_vec3_copy(pt, pt2);
            pt1[dim] -= radius;
            pt2[dim] += radius;
            drawDebugVisLine(pt1, pt2, type);
        }
    }

    void renderImguiPerformanceStats()
    {
        static const float_t perfTimeToMS = 1000.0f / (float_t)SDL_GetPerformanceFrequency();
        ImGui::Text("Simulation Times");
        ImGui::Text((std::format("{:.2f}", perfStats.simTimesUS[perfStats.simTimesUSHeadIndex] * perfTimeToMS) + "ms").c_str());
        ImGui::PlotHistogram("##Simulation Times Histogram", perfStats.simTimesUS, (int32_t)perfStats.simTimesUSCount, (int32_t)perfStats.simTimesUSHeadIndex, "", 0.0f, perfStats.highestSimTime, ImVec2(256, 24.0f));
        ImGui::SameLine();
        ImGui::Text(("[0, " + std::format("{:.2f}", perfStats.highestSimTime * perfTimeToMS) + "]").c_str());
    }

    void renderDebugVisualization(VkCommandBuffer cmd)
    {
        GPUVisCameraData cd = {};
        glm_mat4_copy(engine->_camera->sceneCamera.gpuCameraData.projectionView, cd.projectionView);

        void* data;
        vmaMapMemory(engine->_allocator, visCameraBuffer._allocation, &data);
        memcpy(data, &cd, sizeof(GPUVisCameraData));
        vmaUnmapMemory(engine->_allocator, visCameraBuffer._allocation);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugVisPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugVisPipelineLayout, 0, 1, &debugVisDescriptor, 0, nullptr);

        const VkDeviceSize offsets[1] = { 0 };

        // Draw character collisions (cylinder).  @FUTURE: get an actual cylinder mesh.
        if (engine->generateCollisionDebugVisualization)
        {
            vkCmdBindVertexBuffers(cmd, 0, 1, &capsuleVisVertexBuffer._buffer, offsets);
            for (size_t i = 0; i < numCapsCreated; i++)
            {
                CapsulePhysicsData& cpd = capsulePool[capsuleIndices[i]];

                vec3 comPosition;
                physengine::getCharacterPosition(cpd, comPosition);

                GPUVisInstancePushConst pc = {};
                glm_vec4_copy(vec4{ 0.25f, 1.0f, 0.0f, 1.0f }, pc.color1);
                glm_vec4_copy(pc.color1, pc.color2);
                glm_vec4(comPosition, 0.0f, pc.pt1);
                glm_vec4_add(pc.pt1, vec4{ 0.0f, -cpd.height * 0.5f, 0.0f, 0.0f }, pc.pt1);
                glm_vec4(comPosition, 0.0f, pc.pt2);
                glm_vec4_add(pc.pt2, vec4{ 0.0f, cpd.height * 0.5f, 0.0f, 0.0f }, pc.pt2);

                // @NOCHECKIN @TODO: use this for the capsules in the hitbox system
                //                   (i.e. move it to there).
                vec3 capsuleUp;
                glm_vec3_sub(pc.pt2, pc.pt1, capsuleUp);
                glm_vec3_normalize(capsuleUp);
                versor rotV;
                glm_quat_from_vecs(vec3{ 0.0f, 1.0f, 0.0f }, capsuleUp, rotV);
                glm_quat_mat4(rotV, pc.capsuleRotation);
                ///////////////////////////////////////////////////////////////////

                pc.capsuleRadius = cpd.radius;
                vkCmdPushConstants(cmd, debugVisPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUVisInstancePushConst), &pc);

                vkCmdDraw(cmd, capsuleVisVertexCount, 1, 0, 0);
            }
        }

        // Draw hit capsules.
        if (engine->generateCollisionDebugVisualization)
        {
            vkCmdBindVertexBuffers(cmd, 0, 1, &capsuleVisVertexBuffer._buffer, offsets);

            auto& bodyInterface = physicsSystem->GetBodyInterface();
            size_t processedElems = 0;
            for (size_t i = 0;
                i < sbhcsPool.kPoolSize && processedElems < sbhcsPool._poolElemCount;
                i++)
            {
                auto& sbhcswi = sbhcsPool._pool[i];
                if (sbhcswi.metadata & sbhcsPool.kDeletedMask)
                    continue;  // Skip over deleted elements.

                // Draw each sub shape capsule.
                MutableCompoundShape* shape =
                    static_cast<MutableCompoundShape*>(const_cast<Shape*>(bodyInterface.GetShape(sbhcswi.sbhcs.bodyId).GetPtr()));
                Vec3 bodyPosCom;
                Quat bodyRot;
                bodyInterface.GetPositionAndRotation(sbhcswi.sbhcs.bodyId, bodyPosCom, bodyRot);

                uint32_t count = shape->GetNumSubShapes();
                for (uint32_t i = 0; i < count; i++)
                {
                    auto& subShape = shape->GetSubShape(i);
                    Vec3 posCom = bodyPosCom + bodyRot * subShape.GetPositionCOM();
                    Quat rot = bodyRot * subShape.GetRotation();

                    vec3 posComGlm = {
                        posCom.GetX(),
                        posCom.GetY(),
                        posCom.GetZ(),
                    };
                    versor rotGlm = {
                        rot.GetX(),
                        rot.GetY(),
                        rot.GetZ(),
                        rot.GetW(),
                    };

                    // @NOCHECKIN: Assume that the order of creation of subshapes is the same order of iteration here.
                    auto& subShapeData = sbhcswi.sbhcs.hitCapsuleSubShapes[i];
                    float_t height = subShapeData.height;
                    float_t radius = subShapeData.radius;

                    // Calc push constants.
                    GPUVisInstancePushConst pc = {};
                    glm_vec4_copy(
                        (subShapeData.active ?
                            vec4{ 0.25f, 0.0f, 1.0f, 1.0f } :
                            vec4{ 0.75f, 0.75f, 0.0f, 1.0f }),
                        pc.color1
                    );
                    glm_vec4_copy(pc.color1, pc.color2);

                    mat4 rotMat4;
                    glm_quat_mat4(rotGlm, rotMat4);
                    vec3 offset1;
                    glm_mat4_mulv3(rotMat4, vec3{ 0.0f, -height * 0.5f, 0.0f }, 0.0f, offset1);
                    vec3 offset2;
                    glm_mat4_mulv3(rotMat4, vec3{ 0.0f,  height * 0.5f, 0.0f }, 0.0f, offset2);

                    glm_vec3_add(posComGlm, offset1, pc.pt1);
                    glm_vec3_add(posComGlm, offset2, pc.pt2);
                    pc.pt1[3] = 1.0f;
                    pc.pt2[3] = 1.0f;

                    glm_mat4_copy(rotMat4, pc.capsuleRotation);
                    pc.capsuleRadius = radius;

                    // Draw.
                    vkCmdPushConstants(cmd, debugVisPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUVisInstancePushConst), &pc);
                    vkCmdDraw(cmd, capsuleVisVertexCount, 1, 0, 0);
                }

                processedElems++;
            }
        }

        // Draw lines
        // @NOTE: draw all lines all the time, bc `generateCollisionDebugVisualization` controls creation of the lines (when doing a raycast only), not the drawing.
        std::vector<DebugVisLine> visLinesCopy;
        {
            // Copy debug vis lines so locking time is minimal.
            std::lock_guard<std::mutex> lg(mutateDebugVisLines);
            visLinesCopy = debugVisLines;
        }
        vkCmdBindVertexBuffers(cmd, 0, 1, &lineVisVertexBuffer._buffer, offsets);
        for (DebugVisLine& dvl : visLinesCopy)
        {
            GPUVisInstancePushConst pc = {};
            switch (dvl.type)
            {
                case DebugVisLineType::PURPTEAL:
                    glm_vec4_copy(vec4{ 0.75f, 0.0f, 1.0f, 1.0f }, pc.color1);
                    glm_vec4_copy(vec4{ 0.0f, 0.75f, 1.0f, 1.0f }, pc.color2);
                    break;

                case DebugVisLineType::AUDACITY:
                    glm_vec4_copy(vec4{ 0.0f, 0.1f, 0.5f, 1.0f }, pc.color1);
                    glm_vec4_copy(vec4{ 0.0f, 0.25f, 1.0f, 1.0f }, pc.color2);
                    break;

                case DebugVisLineType::SUCCESS:
                    glm_vec4_copy(vec4{ 0.1f, 0.1f, 0.1f, 1.0f }, pc.color1);
                    glm_vec4_copy(vec4{ 0.0f, 1.0f, 0.7f, 1.0f }, pc.color2);
                    break;

                case DebugVisLineType::VELOCITY:
                    glm_vec4_copy(vec4{ 0.75f, 0.2f, 0.1f, 1.0f }, pc.color1);
                    glm_vec4_copy(vec4{ 1.0f, 0.0f, 0.0f, 1.0f }, pc.color2);
                    break;

                case DebugVisLineType::KIKKOARMY:
                    glm_vec4_copy(vec4{ 0.0f, 0.0f, 0.0f, 1.0f }, pc.color1);
                    glm_vec4_copy(vec4{ 0.0f, 0.25f, 0.0f, 1.0f }, pc.color2);
                    break;

                case DebugVisLineType::YUUJUUFUDAN:
                    glm_vec4_copy(vec4{ 0.69f, 0.69f, 0.69f, 1.0f }, pc.color1);
                    glm_vec4_copy(vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, pc.color2);
                    break;
            }
            glm_vec4(dvl.pt1, 0.0f, pc.pt1);
            glm_vec4(dvl.pt2, 0.0f, pc.pt2);
            vkCmdPushConstants(cmd, debugVisPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUVisInstancePushConst), &pc);

            vkCmdDraw(cmd, lineVisVertexCount, 1, 0, 0);
        }
    }
#endif
}