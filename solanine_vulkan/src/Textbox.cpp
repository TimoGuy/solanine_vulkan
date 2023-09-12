#include "Textbox.h"

#include "TextMesh.h"
#include "InputManager.h"
#include "VulkanEngine.h"
#include "AudioEngine.h"
#include "VkPipelineBuilderUtil.h"
#include "VkInitializers.h"


namespace textbox
{
    vec3 mainRenderPosition            = { 0.0f, -360.0f, 0.0f };
    vec3 mainRenderExtents             = { 400.0f, 100.0f, 1.0f };
    vec3 querySelectionsRenderPosition = { 0.0f, -100.0f, 0.0f };

    textmesh::TextMesh* myText = nullptr;
    std::vector<TextboxMessage> messageQueue;

    size_t currentTextIndex = 0;

    bool answeringQuery = false;
    std::vector<textmesh::TextMesh*> querySelectionTexts;
    uint32_t answeringQuerySelection = 0;
    uint32_t numQuerySelections = 0;

    VulkanEngine* engine;
    AllocatedBuffer sqrVertexBuffer;
    AllocatedBuffer sqrIndexBuffer;
    uint32_t sqrIndexCount;
    VkPipeline textboxBgPipeline;
    VkPipelineLayout textboxBgPipelineLayout;

    void init(VulkanEngine* e)
    {
        engine = e;
    }

    void initBgMesh()
    {
        //
        // Create square mesh for rendering
        //
        std::array<textmesh::Vertex, 4> vertices = {
            textmesh::Vertex{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f } },
            textmesh::Vertex{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } },
            textmesh::Vertex{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
            textmesh::Vertex{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
        };
        std::array<uint32_t, 6> indices = {
            0, 2, 1,
            0, 3, 2,
        };
        sqrIndexCount = (uint32_t)indices.size();

        // Generate host accessible buffers for the text vertices and indices and upload the data
        // @TODO: make vertex and index buffers easier to create and upload data to (i.e. create a lib helper function)
        size_t vertexBufferSize = vertices.size() * sizeof(textmesh::Vertex);
        AllocatedBuffer vertexStaging =
            engine->createBuffer(
                vertexBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY
            );
        sqrVertexBuffer =
            engine->createBuffer(
                vertexBufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY
            );

        size_t indexBufferSize = indices.size() * sizeof(uint32_t);
        AllocatedBuffer indexStaging =
            engine->createBuffer(
                indexBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY
            );
        sqrIndexBuffer =
            engine->createBuffer(
                indexBufferSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY
            );

        // Copy vertices and indices to cpu-side buffers
        void* data;
        vmaMapMemory(engine->_allocator, vertexStaging._allocation, &data);
        memcpy(data, vertices.data(), vertexBufferSize);
        vmaUnmapMemory(engine->_allocator, vertexStaging._allocation);
        vmaMapMemory(engine->_allocator, indexStaging._allocation, &data);
        memcpy(data, indices.data(), indexBufferSize);
        vmaUnmapMemory(engine->_allocator, indexStaging._allocation);

        // Transfer cpu-side staging buffers to gpu-side buffers
        engine->immediateSubmit([&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = vertexBufferSize,
            };
            vkCmdCopyBuffer(cmd, vertexStaging._buffer, sqrVertexBuffer._buffer, 1, &copyRegion);

            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(cmd, indexStaging._buffer, sqrIndexBuffer._buffer, 1, &copyRegion);
            });

        // Destroy staging buffers
        vmaDestroyBuffer(engine->_allocator, vertexStaging._buffer, vertexStaging._allocation);
        vmaDestroyBuffer(engine->_allocator, indexStaging._buffer, indexStaging._allocation);
    }

    void initPipeline(VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor, DeletionQueue& deletionQueue)
    {
        if (textboxBgPipeline == VK_NULL_HANDLE)
            initBgMesh();  // First time. Construct the bg mesh that the pipeline is gonna interact with.

        // Setup vertex descriptions
        // @COPYPASTA TextMesh.cpp
        VkVertexInputAttributeDescription posAttribute = {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(textmesh::Vertex, pos),
        };
        VkVertexInputAttributeDescription uvAttribute = {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(textmesh::Vertex, uv),
        };
        std::vector<VkVertexInputAttributeDescription> attributes = { posAttribute, uvAttribute };

        VkVertexInputBindingDescription mainBinding = {
            .binding = 0,
            .stride = sizeof(textmesh::Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        std::vector<VkVertexInputBindingDescription> bindings = { mainBinding };

        // Setup color blend attachment state
        VkPipelineColorBlendAttachmentState blendAttachmentState = vkinit::colorBlendAttachmentState();
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

        // Build pipeline
        vkutil::pipelinebuilder::build(
            {
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    .offset = 0,
                    .size = sizeof(textmesh::GPUSDFFontPushConstants)
                },
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .offset = sizeof(textmesh::GPUSDFFontPushConstants),
                    .size = sizeof(ColorPushConstBlock)
                },
            },
            { textmesh::gpuUICameraSetLayout },
            {
                { VK_SHADER_STAGE_VERTEX_BIT, "shader/sdf.vert.spv" },
                { VK_SHADER_STAGE_FRAGMENT_BIT, "shader/color_textbox_bg.frag.spv" },
            },
            attributes,
            bindings,
            vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
            screenspaceViewport,
            screenspaceScissor,
            vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT),
            { blendAttachmentState },
            vkinit::multisamplingStateCreateInfo(),
            vkinit::depthStencilCreateInfo(false, false, VK_COMPARE_OP_NEVER),
            {},
            engine->_uiRenderPass,
            0,
            textboxBgPipeline,
            textboxBgPipelineLayout,
            deletionQueue
        );
    }

    void cleanup()
    {
        // Destroy square mesh for rendering
        vmaDestroyBuffer(engine->_allocator, sqrVertexBuffer._buffer, sqrVertexBuffer._allocation);
        vmaDestroyBuffer(engine->_allocator, sqrIndexBuffer._buffer, sqrIndexBuffer._allocation);
    }

    void update(const float_t& unscaledDeltaTime)
    {
        if (myText == nullptr)
            return;

        if (input::onKeyJumpPress)
        {
            // Make selection for query
            if (answeringQuery)
            {
                messageQueue.front().endingQuery.querySelectedCallback(answeringQuerySelection);

                // Answering query cleanup
                answeringQuery = false;
                answeringQuerySelection = 0;
                for (textmesh::TextMesh* tm : querySelectionTexts)
                    textmesh::destroyAndUnregisterTextMesh(tm);
                querySelectionTexts.clear();
            }

            // Advance the textbox
            currentTextIndex++;

            // If there is a query, then set up to answer the query
            if (currentTextIndex == messageQueue.front().texts.size() - 1 &&
                messageQueue.front().useEndingQuery)
            {
                answeringQuery = true;
                answeringQuerySelection = 0;
                numQuerySelections = (uint32_t)messageQueue.front().endingQuery.queryOptions.size();
                for (uint32_t i = 0; i < numQuerySelections; i++)
                {
                    querySelectionTexts.push_back(
                        textmesh::createAndRegisterTextMesh("defaultFont", textmesh::LEFT, textmesh::MID, messageQueue.front().endingQuery.queryOptions[(size_t)i])
                    );
                    querySelectionTexts.back()->excludeFromBulkRender = true;
                    querySelectionTexts.back()->isPositionScreenspace = true;
                    glm_vec3_copy(querySelectionsRenderPosition, querySelectionTexts.back()->renderPosition);
                }
            }

            // If reached end of textbox text, delete front message and reset textbox index
            if (currentTextIndex >= messageQueue.front().texts.size())
            {
                messageQueue.erase(messageQueue.begin());
                currentTextIndex = 0;
            }

            // Destroy textbox mesh if no more textbox messages
            if (messageQueue.empty())
            {
                AudioEngine::getInstance().playSound("res/sfx/wip_Sys_Talk_End.wav");
                textmesh::destroyAndUnregisterTextMesh(myText);
                myText = nullptr;
            }
            else
            {
                AudioEngine::getInstance().playSound("res/sfx/wip_Sys_Talk_Next.wav");
                textmesh::regenerateTextMeshMesh(myText, messageQueue.front().texts[currentTextIndex]);
            }
        }

        // Cycle thru query selections
        static bool prevKeyUpPressed = false;
        if (input::keyUpPressed && !prevKeyUpPressed && answeringQuery)
            answeringQuerySelection = (answeringQuerySelection + numQuerySelections - 1) % numQuerySelections;
        prevKeyUpPressed = input::keyUpPressed;

        static bool prevKeyDownPressed = false;
        if (input::keyDownPressed && !prevKeyDownPressed && answeringQuery)
            answeringQuerySelection = (answeringQuerySelection + 1) % numQuerySelections;
        prevKeyDownPressed = input::keyDownPressed;
    }
    
    bool isProcessingMessage()
    {
        return (myText != nullptr);
    }

    void sendTextboxMessage(TextboxMessage message)
    {
        if (myText == nullptr)
        {
            AudioEngine::getInstance().playSound("res/sfx/wip_Sys_Do_Start.wav");
            currentTextIndex = 0;
            myText = textmesh::createAndRegisterTextMesh("defaultFont", textmesh::CENTER, textmesh::MID, message.texts[currentTextIndex]);
            myText->excludeFromBulkRender = true;
            myText->isPositionScreenspace = true;
            myText->scale = 50.0f;
            glm_vec3_copy(mainRenderPosition, myText->renderPosition);
        }
        messageQueue.push_back(message);
    }

    void renderTextboxBg(VkCommandBuffer cmd)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textboxBgPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textboxBgPipelineLayout, 0, 1, &textmesh::gpuUICameraDescriptorSet, 0, nullptr);

        textmesh::GPUSDFFontPushConstants pc = {
            .modelMatrix = GLM_MAT4_IDENTITY_INIT,
            .renderInScreenspace = (float_t)true,
        };
        glm_translate(pc.modelMatrix, mainRenderPosition);
        glm_scale(pc.modelMatrix, mainRenderExtents);

        ColorPushConstBlock cpc = {
            .color = { 0.0f, 0.0f, 0.0f, 0.5f },
        };

        vkCmdPushConstants(cmd, textboxBgPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(textmesh::GPUSDFFontPushConstants), &pc);
        vkCmdPushConstants(cmd, textboxBgPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(textmesh::GPUSDFFontPushConstants), sizeof(ColorPushConstBlock), &cpc);

        const VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &sqrVertexBuffer._buffer, offsets);
        vkCmdBindIndexBuffer(cmd, sqrIndexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, sqrIndexCount, 1, 0, 0, 0);
    }

    void renderTextbox(VkCommandBuffer cmd)
    {
        if (myText != nullptr)
        {
            renderTextboxBg(cmd);
            textmesh::renderTextMesh(cmd, *myText, true);
        }
    }
}