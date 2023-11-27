#include "pch.h"

#include "MaterialOrganizer.h"


namespace materialorganizer
{
    struct UniqueMaterialBase
    {
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

                enum class Mapping
                {
                    ONE_TO_ONE,
                    TEXTURE_INDEX,
                    TO_FLOAT,
                } mapping;

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

        bool loaded = false;


        void loadFromFile(const std::filesystem::path& path)
        {
            umbPath = path;

            std::ifstream infile(umbPath);  // @TODO: continue from here!
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
                        if (line != "HAwsoo Little texture mixing Format Scrumdiddly Titillating Enticing Procedure (uchar only!)")
                        {
                            std::cerr << "ERROR: file tag missing." << std::endl;
                            return;
                        }
                        stage++;
                        break;

                    case 1:
                    {
                        std::string p1, p2;
                        size_t nextWS;
                        if ((nextWS = line.find(' ')) == std::string::npos)
                        {
                            std::cerr << "[LOAD HALF STEP RECIPE]" << std::endl
                                << "ERROR: line does not have 2 arguments." << std::endl;
                            return;
                        }
                        else
                        {
                            p1 = line.substr(0, nextWS);
                            p2 = line.substr(nextWS);
                            trim(p1);
                            trim(p2);
                        }

                        // Load channel image or scale.
                        std::string channel = p1.substr(0, 1);
                        std::string resType = p1.substr(2);
                        Channel* myChannel = nullptr;
                        if (channel == "r")
                            myChannel = &r;
                        else if (channel == "g")
                            myChannel = &g;
                        else if (channel == "b")
                            myChannel = &b;
                        else if (channel == "a")
                            myChannel = &a;
                        else
                        {
                            std::cerr << "ERROR: channel name not found." << std::endl;
                            return;
                        }

                        myChannel->used = true;
                        if (resType == "file")
                            myChannel->bwImagePath = "res/texture_pool/" + p2;
                        else if (resType == "scale")
                            myChannel->scale = std::stof(p2);
                        else
                        {
                            std::cerr << "ERROR: resource type not found." << std::endl;
                            return;
                        }
                    } break;
                }
            }

            // Post loading check.
            bool channelUsed = false;
            bool imageUsed = false;
            if (r.used)
            {
                channelUsed = true;
                if (!r.bwImagePath.empty())
                    imageUsed = true;
            }
            if (g.used)
            {
                channelUsed = true;
                if (!g.bwImagePath.empty())
                    imageUsed = true;
            }
            if (b.used)
            {
                channelUsed = true;
                if (!b.bwImagePath.empty())
                    imageUsed = true;
            }
            if (a.used)
            {
                channelUsed = true;
                if (!a.bwImagePath.empty())
                    imageUsed = true;
            }

            if (!channelUsed)
            {
                std::cerr << "ERROR: no channel was used." << std::endl;
                return;
            }
            if (!imageUsed)
            {
                std::cerr << "ERROR: no image was used." << std::endl;
                return;
            }

            // SUCCESS! Finish.
            outputPath = "res/texture_pool/_mid_gen_textures/" + path.stem().string() + ".png";
            loaded = true;
        }
    };
    std::vector<UniqueMaterialBase> existingUMBs;

    bool checkMaterialBaseReloadNeeded(const std::filesystem::path& path)
    {
        // @TODO: Search to see if the path is used in `umbPath` for an existing material base, then use that. If not, create a new one.
        UniqueMaterialBase umb(path);
        if (!umb.loaded)
            return false;
        return umb.reloadNeeded();
    }

    bool loadMaterialBase(const std::filesystem::path& path)
    {
        return true;
    }

    bool checkDerivedMaterialParamReloadNeeded(const std::filesystem::path& path)
    {
        return true;
    }

    bool loadDerivedMaterialParam(const std::filesystem::path& path)
    {
        return true;
    }
}
