#include "UIQuad.h"

#include <array>
#include <functional>
#include "TextMesh.h"
#include "VulkanEngine.h"
#include "VkDescriptorBuilderUtil.h"
#include "VkPipelineBuilderUtil.h"
#include "VkInitializers.h"


namespace ui
{
    VulkanEngine* engine;
    
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;
    uint32_t indexCount;

    VkPipeline texturedQuadPipeline;
    VkPipelineLayout texturedQuadPipelineLayout;
    VkPipeline colorQuadPipeline;
    VkPipelineLayout colorQuadPipelineLayout;

    void init(VulkanEngine* e)
    {
        engine = e;
    }

    void initMesh()
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
        indexCount = (uint32_t)indices.size();

        // Generate host accessible buffers for the text vertices and indices and upload the data
        // @TODO: make vertex and index buffers easier to create and upload data to (i.e. create a lib helper function)
        size_t vertexBufferSize = vertices.size() * sizeof(textmesh::Vertex);
        AllocatedBuffer vertexStaging =
            engine->createBuffer(
                vertexBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY
            );
        vertexBuffer =
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
        indexBuffer =
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
            vkCmdCopyBuffer(cmd, vertexStaging._buffer, vertexBuffer._buffer, 1, &copyRegion);

            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(cmd, indexStaging._buffer, indexBuffer._buffer, 1, &copyRegion);
            });

        // Destroy staging buffers
        vmaDestroyBuffer(engine->_allocator, vertexStaging._buffer, vertexStaging._allocation);
        vmaDestroyBuffer(engine->_allocator, indexStaging._buffer, indexStaging._allocation);
    }

    void initDescriptor(UIQuad* uiQuad)
    {
		VkDescriptorImageInfo imageInfo = {
			.sampler = uiQuad->texture->sampler,
			.imageView = uiQuad->texture->imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

        vkutil::DescriptorBuilder::begin()
            .bindImage(0, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build(uiQuad->builtTextureSet);
    }

    void initPipeline(VkViewport& screenspaceViewport, VkRect2D& screenspaceScissor, DeletionQueue& deletionQueue)
    {
        if (texturedQuadPipeline == VK_NULL_HANDLE)
            initMesh();  // First time.

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
                    .size = sizeof(UIQuadSettingsConstBlock)
                },
            },
            { textmesh::gpuUICameraSetLayout, engine->_singleTextureSetLayout },
            {
                { VK_SHADER_STAGE_VERTEX_BIT, "shader/sdf.vert.spv" },
                { VK_SHADER_STAGE_FRAGMENT_BIT, "shader/textured_ui_quad.frag.spv" },
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
            texturedQuadPipeline,
            texturedQuadPipelineLayout,
            deletionQueue
        );

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
                { VK_SHADER_STAGE_FRAGMENT_BIT, "shader/color_ui_quad.frag.spv" },
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
            colorQuadPipeline,
            colorQuadPipelineLayout,
            deletionQueue
        );
    }

    void cleanup()
    {
        // Destroy square mesh for rendering
        vmaDestroyBuffer(engine->_allocator, vertexBuffer._buffer, vertexBuffer._allocation);
        vmaDestroyBuffer(engine->_allocator, indexBuffer._buffer, indexBuffer._allocation);
    }

    std::vector<UIQuad*> registeredUIQuads;

    UIQuad* registerUIQuad(Texture* texture)
    {
        UIQuad* ret = new UIQuad;
        ret->texture = texture;
        if (ret->texture)
            initDescriptor(ret);

        registeredUIQuads.push_back(ret);
        return ret;
    }

    void unregisterUIQuad(UIQuad* toDelete)
    {
        registeredUIQuads.erase(
            std::remove(
                registeredUIQuads.begin(),
                registeredUIQuads.end(),
                toDelete
            ),
            registeredUIQuads.end()
        );
    }

    void renderQuads(VkCommandBuffer cmd, const std::vector<UIQuad*>& sortedUIQuads)
    {

        textmesh::GPUSDFFontPushConstants pc = {
            .renderInScreenspace = (float_t)true,
        };
        ColorPushConstBlock cpc = {};
        UIQuadSettingsConstBlock uqspc = {};
        
        VkPipeline pipeline = texturedQuadPipeline;
        VkPipelineLayout pipelineLayout = texturedQuadPipelineLayout;
        bool prevIsTextured = false;
        bool first = true;
        for (size_t i = 0; i < sortedUIQuads.size(); i++)
        {
            if (!sortedUIQuads[i]->visible)
                continue;

            bool isTextured = (sortedUIQuads[i]->texture != nullptr);
            if (first || isTextured != prevIsTextured)
            {
                pipeline = isTextured ? texturedQuadPipeline : colorQuadPipeline;
                pipelineLayout = isTextured ? texturedQuadPipelineLayout : colorQuadPipelineLayout;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &textmesh::gpuUICameraDescriptorSet, 0, nullptr);
                first = false;
                prevIsTextured = isTextured;
            }

            glm_vec4_copy(sortedUIQuads[i]->tint, (isTextured ? uqspc.tint : cpc.color));

            glm_mat4_identity(pc.modelMatrix);
            glm_translate(pc.modelMatrix, sortedUIQuads[i]->position);
            glm_quat_rotate(pc.modelMatrix, sortedUIQuads[i]->rotation, pc.modelMatrix);
            glm_scale(pc.modelMatrix, sortedUIQuads[i]->scale);

            if (isTextured)
            {
                uqspc.useNineSlicing = (float_t)sortedUIQuads[i]->useNineSlicing;
                if (sortedUIQuads[i]->useNineSlicing)
                {
                    uqspc.nineSlicingBoundX1 = sortedUIQuads[i]->nineSlicingSizeX / sortedUIQuads[i]->scale[0];  // Scaling the nineslicing from units to uv coordinate units.
                    uqspc.nineSlicingBoundY1 = sortedUIQuads[i]->nineSlicingSizeY / sortedUIQuads[i]->scale[1];
                    uqspc.nineSlicingBoundX2 = 1.0f - uqspc.nineSlicingBoundX1;
                    uqspc.nineSlicingBoundY2 = 1.0f - uqspc.nineSlicingBoundY1;
                }
            }

            if (isTextured)
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &sortedUIQuads[i]->builtTextureSet, 0, nullptr);
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(textmesh::GPUSDFFontPushConstants), &pc);
            if (isTextured)
                vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(textmesh::GPUSDFFontPushConstants), sizeof(UIQuadSettingsConstBlock), &uqspc);
            else
                vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(textmesh::GPUSDFFontPushConstants), sizeof(ColorPushConstBlock), &cpc);

            const VkDeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer._buffer, offsets);
            vkCmdBindIndexBuffer(cmd, indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        }
    }

    void renderUIQuads(VkCommandBuffer cmd)
    {
        std::sort(
            registeredUIQuads.begin(),
            registeredUIQuads.end(),
            [&](UIQuad* a, UIQuad* b) {
                return a->renderOrder > b->renderOrder;
            }
        );
        renderQuads(cmd, registeredUIQuads);
    }
}