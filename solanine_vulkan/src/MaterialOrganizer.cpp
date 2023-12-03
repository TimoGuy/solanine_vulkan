#include "pch.h"

#include "MaterialOrganizer.h"

#include "VkDataStructures.h"
#include "VkInitializers.h"
#include "VkTextures.h"
#include "VkDescriptorBuilderUtil.h"
#include "VkPipelineBuilderUtil.h"
#include "VkglTFModel.h"
#include "VulkanEngine.h"
#include "SPIRVReflectionHelper.h"
#include "StringHelper.h"
#include "DataSerialization.h"


namespace materialorganizer
{
    VulkanEngine* engineRef;

    void init(VulkanEngine* engine)
    {
        engineRef = engine;
    }

    // Helper functions.
    void threePartStringParse(std::string line, std::string& outP1, std::string& outP2, std::string& outP3)
    {
        size_t k1 = line.find(' ');
        outP1 = line.substr(0, k1);
        outP2 = line.substr(k1);
        trim(outP1);
        trim(outP2);
        size_t k2 = outP2.find(' ');
        outP3 = outP2.substr(k2);
        outP2 = outP2.substr(0, k2);
        trim(outP2);
        trim(outP3);
    }

    void twoPartStringParse(std::string line, std::string& outP1, std::string& outP2)
    {
        size_t k = line.find(' ');
        outP1 = line.substr(0, k);
        outP2 = line.substr(k);
        trim(outP1);
        trim(outP2);
    }

    // Material Base (.humba)
    struct UniqueMaterialBase
    {
        bool loaded = false;
        std::filesystem::file_time_type lastLoadTime;
        std::filesystem::path umbPath;

        struct ShaderStage
        {
            std::string fname;
            struct Variable
            {
                enum class Type
                {
                    SAMPLER_1D, SAMPLER_2D, SAMPLER_2D_ARRAY, SAMPLER_3D, SAMPLER_CUBE,
                    FLOAT, VEC2, VEC3, VEC4,
                    BOOL,
                    INT,
                    UINT,
                } type;

                bool setTypeFromString(const std::string& s)
                {
                    if (s == "sampler1D")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_1D;
                    else if (s == "sampler2D")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_2D;
                    else if (s == "sampler2DArray")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_2D_ARRAY;
                    else if (s == "sampler3D")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_3D;
                    else if (s == "samplerCube")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_CUBE;
                    else if (s == "float")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::FLOAT;
                    else if (s == "vec2")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::VEC2;
                    else if (s == "vec3")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::VEC3;
                    else if (s == "vec4")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::VEC4;
                    else if (s == "bool")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::BOOL;
                    else if (s == "int")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::INT;
                    else if (s == "uint")
                        type = UniqueMaterialBase::ShaderStage::Variable::Type::UINT;
                    else
                    {
                        std::cerr << "ERROR: material type " << s << " not found." << std::endl;
                        return false;
                    }
                    return true;
                }

                enum class Mapping
                {
                    ONE_TO_ONE,
                    TEXTURE_INDEX,
                    TO_FLOAT,
                } mapping;

                bool setMappingFromString(const std::string& s)
                {
                    if (s == "121")
                        mapping = UniqueMaterialBase::ShaderStage::Variable::Mapping::ONE_TO_ONE;
                    else if (s == "texture_idx")
                        mapping = UniqueMaterialBase::ShaderStage::Variable::Mapping::TEXTURE_INDEX;
                    else if (s == "float")
                        mapping = UniqueMaterialBase::ShaderStage::Variable::Mapping::TO_FLOAT;
                    else
                    {
                        std::cerr << "ERROR: material mapping " << s << " not found." << std::endl;
                        return false;
                    }
                    return true;
                }

                std::string scopedName;
            };
            std::vector<Variable> materialParams;
        };
        ShaderStage vertex, fragment;

        struct Compiled
        {
            bool cooked = false;
            VkPipeline pipeline;
            VkPipelineLayout pipelineLayout;
            AllocatedBuffer materialParamsBuffer;
            VkDescriptorSet materialParamsDescriptorSet;
            VkDescriptorSetLayout materialParamsDescriptorSetLayout;
        } compiled;


        bool loadFromFile(const std::filesystem::path& path)
        {
            umbPath = path;

            std::ifstream infile(umbPath);
            std::string line;
            size_t stage = 0;
            for (size_t lineNum = 1; std::getline(infile, line); lineNum++)
            {
                //
                // Prep line data
                //
                std::string originalLine = line;

                size_t found = line.find('#');
                if (found != std::string::npos)
                {
                    line = line.substr(0, found);
                }

                trim(line);
                if (line.empty())
                    continue;

                switch (stage)
                {
                    case 0:
                        if (line != "Hawsoo Unique Material BAse")
                        {
                            std::cerr << "ERROR: file tag missing." << std::endl;
                            return false;
                        }
                        stage++;
                        break;

                    case 1:
                    {
                        vertex.fname = line;
                        stage++;
                    } break;

                    case 2:
                    {
                        if (line == "---")
                        {
                            stage++;
                            break;
                        }

                        // Parse into 3 parts.
                        std::string p1, p2, p3;
                        threePartStringParse(line, p1, p2, p3);

                        // Put into variables.
                        bool failed = false;
                        UniqueMaterialBase::ShaderStage::Variable matParam = {};
                        failed |= !matParam.setTypeFromString(p1);
                        failed |= !matParam.setMappingFromString(p2);
                        matParam.scopedName = p3;

                        if (!failed)
                            vertex.materialParams.push_back(matParam);
                    } break;

                    case 3:
                    {
                        fragment.fname = line;
                        stage++;
                    } break;

                    case 4:
                    {
                        if (line == "---")
                        {
                            stage++;
                            break;
                        }

                        // Parse into 3 parts.
                        std::string p1, p2, p3;
                        threePartStringParse(line, p1, p2, p3);

                        // Put into variables.
                        bool failed = false;
                        UniqueMaterialBase::ShaderStage::Variable matParam = {};
                        failed |= !matParam.setTypeFromString(p1);
                        failed |= !matParam.setMappingFromString(p2);
                        matParam.scopedName = p3;

                        if (!failed)
                            fragment.materialParams.push_back(matParam);
                    } break;
                }
            }

            lastLoadTime = std::filesystem::file_time_type::clock::now();
            loaded = true;
            return true;
        }

        bool reloadNeeded()
        {
            if (!loaded)
                return true;
            if (std::filesystem::last_write_time(umbPath) >= lastLoadTime)
                return true;
            if (std::filesystem::last_write_time("res/shaders/" + vertex.fname) >= lastLoadTime)
                return true;
            if (std::filesystem::last_write_time("res/shaders/" + fragment.fname) >= lastLoadTime)
                return true;
            return false;
        }
    };
    std::vector<UniqueMaterialBase> existingUMBs;

    bool checkMaterialBaseReloadNeeded(const std::filesystem::path& path)
    {
        UniqueMaterialBase* umb = nullptr;
        for (auto& eUMB : existingUMBs)
            if (eUMB.umbPath == path)
            {
                umb = &eUMB;
                break;
            }
        if (umb == nullptr)
            return true;
        return umb->reloadNeeded();
    }

    bool loadMaterialBase(const std::filesystem::path& path)
    {
        UniqueMaterialBase* umb = nullptr;  // @COPYPASTA
        for (auto& eUMB : existingUMBs)
            if (eUMB.umbPath == path)
            {
                umb = &eUMB;
                break;
            }
        if (umb == nullptr)
        {
            UniqueMaterialBase newUMB = {};
            existingUMBs.push_back(newUMB);
            umb = &existingUMBs.back();
        }
        *umb = {};  // Clear state.
        return umb->loadFromFile(path);
    }

    // Derived Material Parameter Set (.hderriere)
    struct DerivedMaterialParamSet
    {
        bool loaded = false;
        std::filesystem::file_time_type lastLoadTime;
        std::filesystem::path dmpsPath;

        std::string humbaFname;

        struct Param
        {
            std::string scopedName;

            enum class ValueType
            {
                TEXTURE_NAME,
                FLOAT,
                VEC2,
                VEC3,
                VEC4,
                BOOL,
                INT,
                UINT,
            } valueType;

            std::string stringValue;
            vec4 numericalValue;
        };
        std::vector<Param> params;


        bool loadFromFile(const std::filesystem::path& path)
        {
            dmpsPath = path;

            std::ifstream infile(dmpsPath);
            std::string line;
            size_t stage = 0;
            for (size_t lineNum = 1; std::getline(infile, line); lineNum++)
            {
                //
                // Prep line data
                //
                std::string originalLine = line;

                size_t found = line.find('#');
                if (found != std::string::npos)
                {
                    line = line.substr(0, found);
                }

                trim(line);
                if (line.empty())
                    continue;

                switch (stage)
                {
                    case 0:
                        if (line != "Hawsoo DERived MateRIal parametER Entry")
                        {
                            std::cerr << "ERROR: file tag missing." << std::endl;
                            return false;
                        }
                        stage++;
                        break;

                    case 1:
                    {
                        std::string p1, p2;
                        twoPartStringParse(line, p1, p2);
                        if (p1 != "HUMBA")
                        {
                            std::cerr << "ERROR: HUMBA filename expected. Received: " << line << std::endl;
                            return false;
                        }
                        humbaFname = p2;
                        stage++;
                    } break;

                    case 2:
                    {
                        std::string p1, p2;
                        twoPartStringParse(line, p1, p2);

                        DerivedMaterialParamSet::Param newParam = {};
                        newParam.scopedName = p1;

                        bool isNumerical = (p2.find_first_not_of("0123456789-.,") == std::string::npos);
                        if (isNumerical)
                        {
                            int32_t numCommas = 0;
                            for (auto c : p2)
                                if (c == ',')
                                    numCommas++;
                            std::replace(p2.begin(), p2.end(), ',', ' ');  // Values separated by space is required for DataSerializer.
                            switch (numCommas)
                            {
                                case 0:
                                {
                                    // Either float, int, or uint.
                                    if (p2.find('.') == std::string::npos)
                                    {
                                        newParam.valueType = DerivedMaterialParamSet::Param::ValueType::INT;
                                        // newParam.valueType = DerivedMaterialParamSet::Param::ValueType::UINT;  // There will have to be some kind of connection to the shader, the .humba file and this to discern whether this is a uint versus a regular int, so assume int.  -Timo 2023/11/27
                                    }
                                    else
                                        newParam.valueType = DerivedMaterialParamSet::Param::ValueType::FLOAT;
                                    DataSerializer ds;
                                    ds.dumpString(p2);
                                    ds.getSerializedData().loadFloat(newParam.numericalValue[0]);
                                } break;

                                case 1:
                                {
                                    // Is Vec2.
                                    newParam.valueType = DerivedMaterialParamSet::Param::ValueType::VEC2;
                                    DataSerializer ds;
                                    ds.dumpString(p2);
                                    vec2 v;
                                    ds.getSerializedData().loadVec2(v);
                                    glm_vec2_copy(v, newParam.numericalValue);
                                } break;

                                case 2:
                                {
                                    // Is Vec3.
                                    newParam.valueType = DerivedMaterialParamSet::Param::ValueType::VEC3;
                                    DataSerializer ds;
                                    ds.dumpString(p2);
                                    vec3 v;
                                    ds.getSerializedData().loadVec3(v);
                                    glm_vec3_copy(v, newParam.numericalValue);
                                } break;

                                case 3:
                                {
                                    // Is Vec4.
                                    newParam.valueType = DerivedMaterialParamSet::Param::ValueType::VEC4;
                                    DataSerializer ds;
                                    ds.dumpString(p2);
                                    ds.getSerializedData().loadQuat(newParam.numericalValue);
                                } break;
                            }
                        }
                        else
                        {
                            // Either bool, or filename.
                            if (p2 == "true" || p2 == "false")
                            {
                                // Is bool.
                                newParam.valueType = DerivedMaterialParamSet::Param::ValueType::BOOL;
                                newParam.numericalValue[0] = (p2 == "true" ? 1.0f : 0.0f);
                            }
                            else
                            {
                                // Is filename.
                                newParam.valueType = DerivedMaterialParamSet::Param::ValueType::TEXTURE_NAME;
                                newParam.stringValue = p2;
                            }
                        }

                        params.push_back(newParam);
                    } break;
                }
            }

            lastLoadTime = std::filesystem::file_time_type::clock::now();
            loaded = true;
            return true;
        }

        bool reloadNeeded()
        {
            if (!loaded)
                return true;
            if (std::filesystem::last_write_time(dmpsPath) >= lastLoadTime)
                return true;
            for (auto param : params)
                if (param.valueType == DerivedMaterialParamSet::Param::ValueType::TEXTURE_NAME &&
                    !param.stringValue.empty() &&
                    std::filesystem::last_write_time("res/texture_cooked/" + param.stringValue) >= lastLoadTime)
                    return true;
            return false;
        }
    };
    std::vector<DerivedMaterialParamSet> existingDMPSs;

    bool checkDerivedMaterialParamReloadNeeded(const std::filesystem::path& path)
    {
        DerivedMaterialParamSet* dmps = nullptr;  // @COPYPASTA
        for (auto& eDMPS : existingDMPSs)
            if (eDMPS.dmpsPath == path)
            {
                dmps = &eDMPS;
                break;
            }
        if (dmps == nullptr)
            return true;
        return dmps->reloadNeeded();
    }

    bool loadDerivedMaterialParam(const std::filesystem::path& path)
    {
        DerivedMaterialParamSet* dmps = nullptr;  // @COPYPASTA
        for (auto& eDMPS : existingDMPSs)
            if (eDMPS.dmpsPath == path)
            {
                dmps = &eDMPS;
                break;
            }
        if (dmps == nullptr)
        {
            DerivedMaterialParamSet newDMPS = {};
            existingDMPSs.push_back(newDMPS);
            dmps = &existingDMPSs.back();
        }
        *dmps = {};  // Clear state.
        bool succ = dmps->loadFromFile(path);

        // Group by unique material bases.
        std::sort(
            existingDMPSs.begin(),
            existingDMPSs.end(),
            [&](DerivedMaterialParamSet a, DerivedMaterialParamSet b) {
                return a.dmpsPath.compare(b.humbaFname) < 0;
            }
        );

        return succ;
    }

    struct TextureNameWithMap
    {
        std::string name;
        Texture map;
    };
    std::vector<TextureNameWithMap> texturesInOrder;

    void cookTextureIndices()
    {
        // Put together unique set of textures.
        texturesInOrder.clear();
        for (auto& dmps : existingDMPSs)
        for (auto& param : dmps.params)
            if (param.valueType == DerivedMaterialParamSet::Param::ValueType::TEXTURE_NAME)
            {
                bool found = false;
                for (size_t i = 0; i < texturesInOrder.size(); i++)
                    if (param.stringValue == texturesInOrder[i].name)
                    {
                        param.numericalValue[0] = i;
                        found = true;
                        break;
                    }
                if (!found)
                {
                    texturesInOrder.push_back({ .name = param.stringValue });
                    param.numericalValue[0] = texturesInOrder.size() - 1;
                }
            }

        // Load textures.
        for (auto& texture : texturesInOrder)
        {
            vkutil::loadKTXImageFromFile(*engineRef, ("res/texture_cooked/" + texture.name + ".hdelicious").c_str(), VK_FORMAT_R8G8B8A8_UNORM, texture.map.image);

            VkImageViewCreateInfo imageInfo = vkinit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, texture.map.image._image, VK_IMAGE_ASPECT_COLOR_BIT, texture.map.image._mipLevels);
            vkCreateImageView(engineRef->_device, &imageInfo, nullptr, &texture.map.imageView);

            VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(static_cast<float_t>(texture.map.image._mipLevels), VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
            vkCreateSampler(engineRef->_device, &samplerInfo, nullptr, &texture.map.sampler);

            engineRef->_swapchainDependentDeletionQueue.pushFunction([=]() {
                vkDestroyImageView(engineRef->_device, texture.map.imageView, nullptr);
                vkDestroySampler(engineRef->_device, texture.map.sampler, nullptr);
            });
        }

        // Build descriptor sets for materials.
        for (auto& umb : existingUMBs)
        {
            // Find derived materials that use this base.
            std::string umbHumba = umb.umbPath.filename().string();
            std::vector<size_t> dmpsIndices;
            bool zprepassSpecialMat = (umbHumba == "zprepass.special.humba");
            bool shadowSpecialMat = (umbHumba == "shadowdepthpass.special.humba");
            for (size_t i = 0; i < existingDMPSs.size(); i++)
                if (zprepassSpecialMat ||
                    shadowSpecialMat ||
                    existingDMPSs[i].humbaFname == umbHumba)
                    dmpsIndices.push_back(i);

            // Load in struct size and offsets for `MaterialCollection.MaterialParam` struct.
            spv_reflect::ShaderModule umbSM;
            if (!reflectionhelper::loadShaderModule(("res/shaders/" + umb.fragment.fname).c_str(), umbSM))
            {
                std::cerr << "ERROR: Cook failed for unique material: " << umb.umbPath << std::endl;
                continue;  // Duck out bc cook failed.
            }
            auto descriptorBindings = reflectionhelper::extractDescriptorBindingsSorted(umbSM);

            bool materialCollectionDescriptorExists =
                reflectionhelper::findDescriptorBindingsWithName(descriptorBindings, {
                    {
                        .bindings = {
                            {
                                .bindingName = "materialCollection",
                                .bindingType = SpvOpTypeStruct,
                                .binding = 0,
                            },
                            {
                                .bindingName = "textureMaps",
                                .bindingType = SpvOpTypeRuntimeArray,
                                .binding = 1,
                            },
                        },
                    },
                });
            if (!materialCollectionDescriptorExists)
            {
                std::cerr << "[COOK EXISTING UMBS]" << std::endl
                    << "ERROR: material collection not found." << std::endl;
                continue; 
            }

            struct StructElement
            {
                std::string paramName;
                uint32_t relativeOffset;
            };
            StructElement materialIDOffsetElem;
            uint32_t materialParamArrayOffset = 0;
            std::vector<StructElement> materialParamStruct;
            uint32_t materialParamsTotalSize = 0;
            for (auto descriptorBinding : descriptorBindings)
            {
                if (descriptorBinding->type_description->op == SpvOpTypeStruct &&
                    std::string(descriptorBinding->type_description->type_name) == "MaterialCollection")
                {
                    for (size_t i = 0; i < descriptorBinding->block.member_count; i++)
                    {
                        auto matCollection = descriptorBinding->block.members[i];
                        if (matCollection.type_description->op == SpvOpTypeInt &&
                            std::string(matCollection.name) == "materialIDOffset")
                        {
                            materialIDOffsetElem.paramName = matCollection.name;
                            materialIDOffsetElem.relativeOffset = matCollection.offset;
                        }
                        else if (matCollection.type_description->op == SpvOpTypeArray &&
                            std::string(matCollection.type_description->type_name) == "MaterialParam")
                        {
                            materialParamArrayOffset = matCollection.offset;
                            for (size_t j = 0; j < matCollection.member_count; j++)
                            {
                                // Add param into param struct list.
                                auto matParam = matCollection.members[j];
                                materialParamStruct.push_back({
                                    .paramName = matParam.name,
                                    .relativeOffset = matParam.offset,
                                });
                            }
                            materialParamsTotalSize = matCollection.array.stride;
                        }
                    }
                }
            }

            // Create descriptor set and attach to material.
            std::vector<VkDescriptorImageInfo> textureMapInfos;
            std::map<std::string, size_t> textureNameToMapIndex;
            {
                size_t i = 0;
                for (auto& texture : texturesInOrder)
                {
                    textureMapInfos.push_back(vkinit::textureToDescriptorImageInfo(&texture.map));
                    textureNameToMapIndex[texture.name] = i++;
                }
            }

            size_t materialParamsBufferSize = materialParamArrayOffset + materialParamsTotalSize * dmpsIndices.size();  // @HACK: first `uint materialIDOffset` is only 4 bytes, but since the `params` array is next, the `params` array has an offset of 16 bytes. Include these extra bytes in the buffer, so don't use the size of `uint materialIDOffset` in the calc for size of buffer.  -Timo 2023/11/30
            umb.compiled.materialParamsBuffer = engineRef->createBuffer(materialParamsBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
            engineRef->_swapchainDependentDeletionQueue.pushFunction([=]() {
                vmaDestroyBuffer(engineRef->_allocator, umb.compiled.materialParamsBuffer._buffer, umb.compiled.materialParamsBuffer._allocation);
            });

            VkDescriptorBufferInfo materialParamsBufferInfo = {
                .buffer = umb.compiled.materialParamsBuffer._buffer,
                .offset = 0,
                .range = materialParamsBufferSize,
            };

            // Upload material param info.
            uint8_t* data;
            vmaMapMemory(engineRef->_allocator, umb.compiled.materialParamsBuffer._allocation, (void**)&data);
            {
                uint32_t materialIDOffset = (uint32_t)dmpsIndices.front();
                memcpy(data + 0, &materialIDOffset, sizeof(uint32_t));
            }
            for (size_t i = 0; i < dmpsIndices.size(); i++)
            {
                size_t dmpsIdx = dmpsIndices[i];
                for (auto& dmpsParam : existingDMPSs[dmpsIdx].params)
                for (auto& matParam : materialParamStruct)
                for (auto& umbParam : umb.fragment.materialParams)
                    if (dmpsParam.scopedName == matParam.paramName &&
                        dmpsParam.scopedName == umbParam.scopedName)
                    {
                        size_t offset = materialParamArrayOffset + materialParamsTotalSize * i + (size_t)matParam.relativeOffset;
                        switch (umbParam.type)
                        {
                            case UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_1D:
                            case UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_2D:
                            case UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_2D_ARRAY:
                            case UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_3D:
                            case UniqueMaterialBase::ShaderStage::Variable::Type::SAMPLER_CUBE:
                            {
                                if (umbParam.mapping != UniqueMaterialBase::ShaderStage::Variable::Mapping::TEXTURE_INDEX)
                                    std::cerr << "ERROR: texture index mapping isn't selected." << std::endl;
                                uint32_t textureIdx = (uint32_t)textureNameToMapIndex[dmpsParam.stringValue];
                                memcpy(data + offset, &textureIdx, sizeof(uint32_t));
                            } break;

                            case UniqueMaterialBase::ShaderStage::Variable::Type::FLOAT:
                            {
                                memcpy(data + offset, &dmpsParam.numericalValue, sizeof(float_t));
                            } break;

                            case UniqueMaterialBase::ShaderStage::Variable::Type::VEC2:
                            {
                                memcpy(data + offset, &dmpsParam.numericalValue, sizeof(vec2));
                            } break;

                            case UniqueMaterialBase::ShaderStage::Variable::Type::VEC3:
                            {
                                memcpy(data + offset, &dmpsParam.numericalValue, sizeof(vec3));
                            } break;

                            case UniqueMaterialBase::ShaderStage::Variable::Type::VEC4:
                            {
                                memcpy(data + offset, &dmpsParam.numericalValue, sizeof(vec4));
                            } break;

                            case UniqueMaterialBase::ShaderStage::Variable::Type::BOOL:
                            {
                                if (umbParam.mapping == UniqueMaterialBase::ShaderStage::Variable::Mapping::TO_FLOAT)
                                {
                                    float_t val = (float_t)dmpsParam.numericalValue[0];  // Bool is already stored as float.
                                    memcpy(data + offset, &val, sizeof(float_t));
                                }
                                else
                                    std::cerr << "ERROR: bool with `float` mapping isn't selected." << std::endl;
                            } break;

                            case UniqueMaterialBase::ShaderStage::Variable::Type::INT:
                            {
                                int32_t val = (int32_t)dmpsParam.numericalValue[0];
                                memcpy(data + offset, &val, sizeof(int32_t));
                            } break;

                            case UniqueMaterialBase::ShaderStage::Variable::Type::UINT:
                            {
                                uint32_t val = (uint32_t)dmpsParam.numericalValue[0];
                                memcpy(data + offset, &val, sizeof(uint32_t));
                            } break;
                        }
                    }
            }
            vmaUnmapMemory(engineRef->_allocator, umb.compiled.materialParamsBuffer._allocation);
            
            vkutil::DescriptorBuilder::begin()
                .bindBuffer(0, &materialParamsBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .bindImageArray(1, (uint32_t)textureMapInfos.size(), textureMapInfos.data(), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .build(umb.compiled.materialParamsDescriptorSet, umb.compiled.materialParamsDescriptorSetLayout);

            engineRef->attachTextureSetToMaterial(umb.compiled.materialParamsDescriptorSet, umbHumba);

            // Load pipeline and attach to material.
            vkglTF::VertexInputDescription modelVertexDescription = vkglTF::Model::Vertex::getVertexDescription();

            VkViewport screenspaceViewport = {
                0.0f, 0.0f,
                (float_t)engineRef->_windowExtent.width, (float_t)engineRef->_windowExtent.height,
                0.0f, 1.0f,
            };
            VkRect2D screenspaceScissor = {
                { 0, 0 },
                engineRef->_windowExtent,
            };

            if (zprepassSpecialMat)
                vkutil::pipelinebuilder::build(
                    {},
                    {
                        engineRef->_globalSetLayout,
                        engineRef->_objectSetLayout,
                        engineRef->_instancePtrSetLayout,
                        umb.compiled.materialParamsDescriptorSetLayout,
                        engineRef->_skeletalAnimationSetLayout,
                    },
                    {
                        { VK_SHADER_STAGE_VERTEX_BIT, ("res/shaders/" + umb.vertex.fname).c_str() },
                        { VK_SHADER_STAGE_FRAGMENT_BIT, ("res/shaders/" + umb.fragment.fname).c_str() },
                    },
                    modelVertexDescription.attributes,
                    modelVertexDescription.bindings,
                    vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                    screenspaceViewport,
                    screenspaceScissor,
                    vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT),
                    {}, // No color attachment for the z prepass pipeline; only writing to depth!
                    vkinit::multisamplingStateCreateInfo(),
                    vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS),
                    {},
                    engineRef->_mainRenderPass,
                    0,
                    umb.compiled.pipeline,
                    umb.compiled.pipelineLayout,
                    engineRef->_swapchainDependentDeletionQueue
                );
            else if (shadowSpecialMat)
            {
                auto shadowRasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
                shadowRasterizer.depthClampEnable = VK_TRUE;
                vkutil::pipelinebuilder::build(
                    {
                        VkPushConstantRange{
                            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                            .offset = 0,
                            .size = sizeof(CascadeIndexPushConstBlock)
                        }
                    },
                    {
                        engineRef->_cascadeViewProjsSetLayout,
                        engineRef->_objectSetLayout,
                        engineRef->_instancePtrSetLayout,
                        umb.compiled.materialParamsDescriptorSetLayout,
                        engineRef->_skeletalAnimationSetLayout,
                    },
                    {
                        { VK_SHADER_STAGE_VERTEX_BIT, ("res/shaders/" + umb.vertex.fname).c_str() },
                        { VK_SHADER_STAGE_FRAGMENT_BIT, ("res/shaders/" + umb.fragment.fname).c_str() },
                    },
                    modelVertexDescription.attributes,
                    modelVertexDescription.bindings,
                    vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                    VkViewport{
                        0.0f, 0.0f,
                        (float_t)SHADOWMAP_DIMENSION, (float_t)SHADOWMAP_DIMENSION,
                        0.0f, 1.0f,
                    },
                    VkRect2D{
                        { 0, 0 },
                        VkExtent2D{ SHADOWMAP_DIMENSION, SHADOWMAP_DIMENSION },
                    },
                    shadowRasterizer,
                    {},  // No color attachment for this pipeline
                    vkinit::multisamplingStateCreateInfo(),
                    vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL),
                    {},
                    engineRef->_shadowRenderPass,
                    0,
                    umb.compiled.pipeline,
                    umb.compiled.pipelineLayout,
                    engineRef->_swapchainDependentDeletionQueue
                );
            }
            else
                vkutil::pipelinebuilder::build(
                    {},
                    {
                        engineRef->_globalSetLayout,
                        engineRef->_objectSetLayout,
                        engineRef->_instancePtrSetLayout,
                        umb.compiled.materialParamsDescriptorSetLayout,
                        engineRef->_skeletalAnimationSetLayout,
                        engineRef->_voxelFieldLightingGridTextureSet.layout,
                    },
                    {
                        { VK_SHADER_STAGE_VERTEX_BIT, ("res/shaders/" + umb.vertex.fname).c_str() },
                        { VK_SHADER_STAGE_FRAGMENT_BIT, ("res/shaders/" + umb.fragment.fname).c_str() },
                    },
                    modelVertexDescription.attributes,
                    modelVertexDescription.bindings,
                    vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
                    screenspaceViewport,
                    screenspaceScissor,
                    vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT),
                    { vkinit::colorBlendAttachmentState() },
                    vkinit::multisamplingStateCreateInfo(),
                    vkinit::depthStencilCreateInfo(true, false, VK_COMPARE_OP_EQUAL),
                    {},
                    engineRef->_mainRenderPass,
                    1,
                    umb.compiled.pipeline,
                    umb.compiled.pipelineLayout,
                    engineRef->_swapchainDependentDeletionQueue
                );
            engineRef->attachPipelineToMaterial(umb.compiled.pipeline, umb.compiled.pipelineLayout, umbHumba);

            // Finished.
            umb.compiled.cooked = true;
        }
    }

    size_t derivedMaterialNameToUMBIdx(std::string derivedMatName)
    {
        derivedMatName += ".hderriere";
        for (auto& dmps : existingDMPSs)
            if (dmps.dmpsPath.filename().string() == derivedMatName)
                for (size_t i = 0; i < existingUMBs.size(); i++)
                    if (existingUMBs[i].umbPath.filename().string() == dmps.humbaFname)
                        return i;
        
        std::cerr << "[DERIVED MATERIAL NAME TO UMB IDX]" << std::endl
            << "ERROR: derived material name " << derivedMatName << " not connected to a UMB." << std::endl;
        return (size_t)-1;
    }

    size_t derivedMaterialNameToDMPSIdx(std::string derivedMatName)
    {
        derivedMatName += ".hderriere";
        for (size_t i = 0; i < existingDMPSs.size(); i++)
            if (existingDMPSs[i].dmpsPath.filename().string() == derivedMatName)
                return i;
        
        std::cerr << "[DERIVED MATERIAL NAME TO DMPS IDX]" << std::endl
            << "ERROR: derived material name " << derivedMatName << " not connected to a DMPS." << std::endl;
        return (size_t)-1;
    }

    std::string umbIdxToUniqueMaterialName(size_t umbIdx)
    {
        return existingUMBs[umbIdx].umbPath.filename().string();
    }
}
