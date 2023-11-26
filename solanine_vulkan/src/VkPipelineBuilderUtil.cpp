#include "pch.h"

#include "VkPipelineBuilderUtil.h"

#include "VkDataStructures.h"
#include "VkInitializers.h"


namespace vkutil
{
    namespace pipelinelayoutcache
    {
        VkDevice device;
        std::vector<VkPipelineLayout> createdLayouts;

        void init(VkDevice newDevice)
        {
            device = newDevice;
        }
        
        void cleanup()
        {
            for (auto& layout : createdLayouts)
                vkDestroyPipelineLayout(device, layout, nullptr);  // @NOTE: user of this is in charge of deleting pipelines, but not pipeline layouts. Also, if the swapchain gets recreated, the pipelines all need to be recreated as well, but not pipeline layouts.
        }

        VkPipelineLayout createPipelineLayout(VkPipelineLayoutCreateInfo* info)
        {
            // @TODO: have a cache system, but for now, just create everything that's thrown to you.
            VkPipelineLayout newLayout;
            VK_CHECK(vkCreatePipelineLayout(device, info, nullptr, &newLayout));
            createdLayouts.push_back(newLayout);
            return newLayout;
        }
    }

    namespace pipelinebuilder
    {
        bool loadShaderModule(const char* filePath, VkShaderModule& outShaderModule)
        {
            std::cout << "[LOAD SHADER MODULE]" << std::endl;

            // Open SPIRV file
            std::ifstream file(filePath, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                std::cerr << "ERROR: could not open file " << filePath << std::endl;
                return false;
            }

            //
            // Get the filesize and copy the whole thing into the correct sized buffer
            //
            size_t filesize = (size_t)file.tellg();
            std::vector<uint32_t> buffer(filesize / sizeof(uint32_t));
            file.seekg(0);
            file.read((char*)buffer.data(), filesize);
            file.close();

            //
            // Load the shader into Vulkan
            //
            VkShaderModuleCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.pNext = nullptr;
            createInfo.codeSize = buffer.size() * sizeof(uint32_t);
            createInfo.pCode = buffer.data();

            // Error check
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(pipelinelayoutcache::device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
            {
                std::cerr << "ERROR: could not create shader module for shader file " << filePath << std::endl;
                return false;
            }

            // Successful shader creation!
            outShaderModule = shaderModule;

            std::cout << "Successfully created shader module for shader file " << filePath << std::endl;
            return true;
        }

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
            DeletionQueue&                                   deletionQueue)
        {
            // Create pipeline layout
            VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipelineLayoutCreateInfo();
            layoutInfo.pPushConstantRanges = pushConstantRanges.data();
            layoutInfo.pushConstantRangeCount = (uint32_t)pushConstantRanges.size();
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
            outPipelineLayout = pipelinelayoutcache::createPipelineLayout(&layoutInfo);

            // Load shaders
            std::vector<VkPipelineShaderStageCreateInfo> compiledShaderStages;
            for (auto& stage : shaderStages)
            {
                VkShaderModule sm;
                loadShaderModule(stage.filePath, sm);
                compiledShaderStages.push_back(
                    vkinit::pipelineShaderStageCreateInfo(stage.stage, sm));
            }

            // Create pipeline
            VkPipelineViewportStateCreateInfo viewportState = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .pNext = nullptr,
                .viewportCount = 1,
                .pViewports = &viewport,
                .scissorCount = 1,
                .pScissors = &scissor,
            };

            VkPipelineColorBlendStateCreateInfo colorBlending = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .pNext = nullptr,
                .logicOpEnable = VK_FALSE,
                .logicOp = VK_LOGIC_OP_COPY,
                .attachmentCount = (uint32_t)colorBlendStates.size(),
                .pAttachments = colorBlendStates.data(),
            };

            VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkinit::vertexInputStateCreateInfo();
            vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();
            vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)vertexAttributes.size();
            vertexInputInfo.pVertexBindingDescriptions = vertexInputBindings.data();
            vertexInputInfo.vertexBindingDescriptionCount = (uint32_t)vertexInputBindings.size();

            VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = (uint32_t)dynamicStates.size(),
                .pDynamicStates = dynamicStates.data(),
            };

            VkGraphicsPipelineCreateInfo pipelineInfo = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext = nullptr,
                .stageCount = (uint32_t)compiledShaderStages.size(),
                .pStages = compiledShaderStages.data(),
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizationState,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencilState,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicStateCreateInfo,
                .layout = outPipelineLayout,
                .renderPass = renderPass,
                .subpass = subpass,
                .basePipelineHandle = VK_NULL_HANDLE,
            };

            // Check for errors while creating gfx pipelines
            std::cout << "[BUILD GRAPHICS PIPELINE]" << std::endl;
            if (vkCreateGraphicsPipelines(pipelinelayoutcache::device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS)
            {
                std::cout << "FAILED: creating pipeline" << std::endl;
                return false;
            }

            std::cout << "Successfully built graphics pipeline" << std::endl;

            // Cleanup
            for (auto shaderStage : compiledShaderStages)
                vkDestroyShaderModule(pipelinelayoutcache::device, shaderStage.module, nullptr);

            std::cout << "Cleaned up used shader modules (count: " << compiledShaderStages.size() << ")" << std::endl;

            // Add pipeline to deletion queue.
            deletionQueue.pushFunction([=]() {
                vkDestroyPipeline(pipelinelayoutcache::device, outPipeline, nullptr);
            });

            return true;
        }
    }
}