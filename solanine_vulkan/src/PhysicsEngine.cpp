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
    std::map<uint32_t, std::string> bodyIdToEntityGuidMap;

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
        static constexpr ObjectLayer NUM_LAYERS = 2;
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
                return inObject2 == Layers::MOVING; // Non moving only collides with moving
            case Layers::MOVING:
                return true; // Moving collides with everything
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
        static constexpr uint32_t NUM_LAYERS(2);
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
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:
                return true;
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
                    uint32_t id = thisBody.GetID().GetIndex();
                    SimulationCharacter* entityAsChar;
                    if (entityAsChar = dynamic_cast<SimulationCharacter*>(entityManager->getEntityViaGUID(bodyIdToEntityGuidMap[id])))
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
            if (runPhysicsSimulations)
                physicsSystem->Update(simDeltaTime, 1, 1, &tempAllocator, &jobSystem);
            copyResultTransforms();

#ifdef _DEVELOP
            {
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
        bodyIdToEntityGuidMap[vfpd.bodyId.GetIndex()] = entityGuid;
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

            // Create physics capsule.
            ShapeRefC capsuleShape = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * height + radius, 0), Quat::sIdentity(), new CapsuleShape(0.5f * height, radius)).Create().Get();

            Ref<CharacterSettings> settings = new CharacterSettings;
            settings->mMaxSlopeAngle = glm_rad(45.0f);
            settings->mLayer = Layers::MOVING;
            settings->mShape = capsuleShape;

            // @NOTE: this was in the past 0.0f, but after introducing the slightest slope, the character starts sliding down.
            //        This gives everything a bit of a tacky feel, but I feel like that makes the physics for the characters
            //        feel real (gives character lol). Plus, the characters can hold up to a rotating moving platform.  -Timo 2023/09/30
            settings->mFriction = 0.0f;

            settings->mSupportingVolume = Plane(Vec3::sAxisY(), -(0.5f * height));
            cpd.character = new JPH::Character(settings, RVec3(position[0], position[1], position[2]), Quat::sIdentity(), (int64_t)UserDataMeaning::IS_CHARACTER, physicsSystem);
            if (enableCCD)
                physicsSystem->GetBodyInterface().SetMotionQuality(cpd.character->GetBodyID(), EMotionQuality::LinearCast);

            cpd.character->AddToPhysicsSystem(EActivation::Activate);
            cpd.simTransformId = registerSimulationTransform();

            // Add guid into references.
            bodyIdToEntityGuidMap[cpd.character->GetBodyID().GetIndex()] = entityGuid;

            return &cpd;
        }
        else
        {
            std::cerr << "ERROR: capsule creation has reached its limit" << std::endl;
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
        simSetOffset++;
    }

    void copyResultTransforms()
    {
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
        size_t simSetOffsetCopy = simSetOffset;
        size_t prevSimSet = (simSetOffsetCopy + 0) % 3;
        size_t currentSimSet = (simSetOffsetCopy + 1) % 3;
        float_t physicsAlpha = getPhysicsAlpha();

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

    bool raycast(vec3 origin, vec3 directionAndMagnitude, std::string& outHitGuid)
    {
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
            const uint32_t bodyIdIdx = result.mBodyID.GetIndex();
            if (bodyIdToEntityGuidMap.find(bodyIdIdx) == bodyIdToEntityGuidMap.end())
            {
                std::cout << "[RAYCAST]" << std::endl
                    << "WARNING: body ID " << bodyIdIdx << " didn\'t match any entity GUIDs." << std::endl;
            }
            else
            {
                outHitGuid = bodyIdToEntityGuidMap[bodyIdIdx];
            }
            return true;
        }
        return false;
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

        // Draw capsules
        if (engine->generateCollisionDebugVisualization)
        {
            vkCmdBindVertexBuffers(cmd, 0, 1, &capsuleVisVertexBuffer._buffer, offsets);
            for (size_t i = 0; i < numCapsCreated; i++)
            {
                CapsulePhysicsData& cpd = capsulePool[capsuleIndices[i]];
                GPUVisInstancePushConst pc = {};
                glm_vec4_copy(vec4{ 0.25f, 1.0f, 0.0f, 1.0f }, pc.color1);
                glm_vec4_copy(pc.color1, pc.color2);
                glm_vec4(cpd.currentCOMPosition, 0.0f, pc.pt1);
                glm_vec4_add(pc.pt1, vec4{ 0.0f, -cpd.height * 0.5f, 0.0f, 0.0f }, pc.pt1);
                glm_vec4(cpd.currentCOMPosition, 0.0f, pc.pt2);
                glm_vec4_add(pc.pt2, vec4{ 0.0f, cpd.height * 0.5f, 0.0f, 0.0f }, pc.pt2);
                pc.capsuleRadius = cpd.radius;
                vkCmdPushConstants(cmd, debugVisPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUVisInstancePushConst), &pc);

                vkCmdDraw(cmd, capsuleVisVertexCount, 1, 0, 0);
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