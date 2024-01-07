#pragma once

struct DeletionQueue;


namespace vkutil
{
    namespace pipelinelayoutcache
    {
        void init(VkDevice newDevice);
        void cleanup();

        VkPipelineLayout createPipelineLayout(VkPipelineLayoutCreateInfo* info);
    }

    namespace pipelinebuilder
    {
        struct ShaderStageInfo
        {
            VkShaderStageFlagBits stage;
            const char* filePath;
        };

        bool loadShaderModule(const char* filePath, VkShaderModule& outShaderModule);

        bool build(
            std::vector<VkPushConstantRange>                 pushConstantRanges,
            std::vector<VkDescriptorSetLayout>               setLayouts,
            std::vector<ShaderStageInfo>                     shaderStages,
            std::vector<VkVertexInputAttributeDescription>   vertexAttributes,
            std::vector<VkVertexInputBindingDescription>     vertexInputBindings,
            VkPipelineInputAssemblyStateCreateInfo           inputAssembly,
            VkViewport                                       viewport,
            VkRect2D                                         scissor,
            VkPipelineRasterizationStateCreateInfo           rasterizationState,
            std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates,
            VkPipelineMultisampleStateCreateInfo             multisampling,
            VkPipelineDepthStencilStateCreateInfo            depthStencilState,
            std::vector<VkDynamicState>                      dynamicStates,
            VkRenderPass                                     renderPass,
            uint32_t                                         subpass,
            VkPipeline&                                      outPipeline,
            VkPipelineLayout&                                outPipelineLayout,
            DeletionQueue&                                   deletionQueue);

        bool buildCompute(
            std::vector<VkPushConstantRange>                 pushConstantRanges,
            std::vector<VkDescriptorSetLayout>               setLayouts,
            ShaderStageInfo                     shaderStage,
            VkPipeline&                                      outPipeline,
            VkPipelineLayout&                                outPipelineLayout,
            DeletionQueue&                                   deletionQueue);
    }
}