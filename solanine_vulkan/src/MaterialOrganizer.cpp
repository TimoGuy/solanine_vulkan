#include "pch.h"

#include "MaterialOrganizer.h"

#include "StringHelper.h"
#include "DataSerialization.h"


namespace materialorganizer
{
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
            VkPipeline pipeline;
            VkPipelineLayout layout;
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
        return dmps->loadFromFile(path);
    }

    std::vector<std::string> textureNamesInOrder;

    void cookTextureIndices()
    {
        textureNamesInOrder.clear();
        for (auto& dmps : existingDMPSs)
        for (auto& param : dmps.params)
            if (param.valueType == DerivedMaterialParamSet::Param::ValueType::TEXTURE_NAME)
            {
                bool found = false;
                for (size_t i = 0; i < textureNamesInOrder.size(); i++)
                    if (param.stringValue == textureNamesInOrder[i])
                    {
                        param.numericalValue[0] = i;
                        found = true;
                        break;
                    }
                if (!found)
                {
                    textureNamesInOrder.push_back(param.stringValue);
                    param.numericalValue[0] = textureNamesInOrder.size() - 1;
                }
            }
    }
}
