#include "pch.h"

#include "TextureCooker.h"

#include "StringHelper.h"


namespace texturecooker
{
    stbi_uc* loadUCharImage(const std::filesystem::path& path, int32_t& outWidth, int32_t& outHeight, int32_t& outNumChannels)
    {
        stbi_uc* pixels = stbi_load(path.string().c_str(), &outWidth, &outHeight, &outNumChannels, STBI_default);  // @NOTE: bc we're not putting this image data into vulkan, we can use however many channels instead of having to force 4.  -Timo 2023/12/4
        if (!pixels)
        {
            std::cerr << "ERROR: failed to load texture " << path << std::endl;
            return nullptr;
        }
        return pixels;
    }

    struct HalfStepRecipe
    {
        bool loaded = false;
        std::filesystem::path outputPath;
        struct Channel
        {
            bool used = false;
            std::filesystem::path bwImagePath;
            float_t scale = -1.0f;
        };
        Channel r, g, b, a;

        HalfStepRecipe(const std::filesystem::path& path)
        {
            std::ifstream infile(path);
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

        bool checkIfNeedToExecute(const std::filesystem::path& halfstepPath)
        {
            if (!loaded)
                return false;

            if (!std::filesystem::exists(outputPath))
                return true;
            
            auto lwt = std::filesystem::last_write_time(outputPath);
            if (lwt <= std::filesystem::last_write_time(halfstepPath))
                return true;
            if (!r.bwImagePath.empty() &&
                std::filesystem::last_write_time(r.bwImagePath) >= lwt)
                return true;
            if (!g.bwImagePath.empty() &&
                std::filesystem::last_write_time(g.bwImagePath) >= lwt)
                return true;
            if (!b.bwImagePath.empty() &&
                std::filesystem::last_write_time(b.bwImagePath) >= lwt)
                return true;
            if (!a.bwImagePath.empty() &&
                std::filesystem::last_write_time(a.bwImagePath) >= lwt)
                return true;
            return false;
        }

        bool loadAndCookFile()
        {
            bool force4Channels = true;  // @HACK: it seems like the toktx.exe tool doesn't really like 24-bit png files, among other things... either way, forcing 4 channel output from the halfstep cooker just makes things run smoother.  -Timo 2023/12/4
            stbi_uc* rUC = nullptr;
            int32_t  rChan;
            stbi_uc* gUC = nullptr;
            int32_t  gChan;
            stbi_uc* bUC = nullptr;
            int32_t  bChan;
            stbi_uc* aUC = nullptr;
            int32_t  aChan;
            int32_t masterW = -1, masterH = -1;
            if (r.used && !r.bwImagePath.empty())
            {
                int32_t w, h;
                rUC = loadUCharImage(r.bwImagePath, w, h, rChan);
                if ((masterW > 0 && masterW != w) ||
                    (masterH > 0 && masterH != h))
                {
                    std::cerr << "ERROR: texture sizes are inconsistent." << std::endl;
                    return false;
                }
                else
                {
                    masterW = w;
                    masterH = h;
                }
            }
            if (g.used && !g.bwImagePath.empty())
            {
                int32_t w, h;
                gUC = loadUCharImage(g.bwImagePath, w, h, gChan);
                if ((masterW > 0 && masterW != w) ||
                    (masterH > 0 && masterH != h))
                {
                    std::cerr << "ERROR: texture sizes are inconsistent." << std::endl;
                    return false;
                }
                else
                {
                    masterW = w;
                    masterH = h;
                }
            }
            if (b.used && !b.bwImagePath.empty())
            {
                int32_t w, h;
                bUC = loadUCharImage(b.bwImagePath, w, h, bChan);
                if ((masterW > 0 && masterW != w) ||
                    (masterH > 0 && masterH != h))
                {
                    std::cerr << "ERROR: texture sizes are inconsistent." << std::endl;
                    return false;
                }
                else
                {
                    masterW = w;
                    masterH = h;
                }
            }
            if (a.used && !a.bwImagePath.empty())
            {
                int32_t w, h;
                aUC = loadUCharImage(a.bwImagePath, w, h, aChan);
                if ((masterW > 0 && masterW != w) ||
                    (masterH > 0 && masterH != h))
                {
                    std::cerr << "ERROR: texture sizes are inconsistent." << std::endl;
                    return false;
                }
                else
                {
                    masterW = w;
                    masterH = h;
                }
            }

            // Write image.
            int32_t channels = (force4Channels || a.used ? 4 : 3);
            stbi_uc* imgData = new stbi_uc[channels * masterW * masterH];
            for (size_t i = 0; i < masterW * masterH; i++)
            {
                size_t pos = i * channels;

                stbi_uc rVal = (r.scale >= 0.0f ? 255 : 0);
                if (rUC != nullptr)
                    rVal = rUC[i * rChan + 0];
                if (r.scale >= 0.0f)
                    rVal *= r.scale;
                imgData[pos + 0] = rVal;

                stbi_uc gVal = (g.scale >= 0.0f ? 255 : 0);
                if (gUC != nullptr)
                    gVal = gUC[i * gChan + 0];
                if (g.scale >= 0.0f)
                    gVal *= g.scale;
                imgData[pos + 1] = gVal;

                stbi_uc bVal = (b.scale >= 0.0f ? 255 : 0);
                if (bUC != nullptr)
                    bVal = bUC[i * bChan + 0];
                if (b.scale >= 0.0f)
                    bVal *= b.scale;
                imgData[pos + 2] = bVal;

                if (channels < 4)
                    continue;  // Exit early if no alpha channel.

                if (a.used)
                {
                    stbi_uc aVal = (a.scale >= 0.0f ? 255 : 0);
                    if (aUC != nullptr)
                        aVal = aUC[i * aChan + 0];
                    if (a.scale >= 0.0f)
                        aVal *= a.scale;
                    imgData[pos + 3] = aVal;
                }
                else
                    imgData[pos + 3] = 255;
            }

            if (rUC != nullptr)
                stbi_image_free(rUC);
            if (gUC != nullptr)
                stbi_image_free(gUC);
            if (bUC != nullptr)
                stbi_image_free(bUC);
            if (aUC != nullptr)
                stbi_image_free(aUC);

            stbi_write_png(
                outputPath.string().c_str(),
                masterW,
                masterH,
                channels,
                imgData,
                channels * masterW * sizeof(stbi_uc)
            );

            return true;
        }
    };

    bool checkHalfStepNeeded(const std::filesystem::path& path)
    {
        HalfStepRecipe hsp(path);
        return hsp.checkIfNeedToExecute(path);
    }

    bool cookHalfStepFromRecipe(const std::filesystem::path& path)
    {
        HalfStepRecipe hsp(path);
        if (!hsp.loaded)
            return false;

        std::cout << "[COOKING HALF STEP]" << std::endl << path.filename() << " to " << hsp.outputPath.filename() << "\t...\t";
        if (hsp.loadAndCookFile())
        {
            std::cout << "SUCCESS" << std::endl;
            return true;
        }

        std::cout << "FAILURE" << std::endl;
        return false;
    }

    struct Recipe
    {
        bool loaded = false;
        std::filesystem::path outputPath;
        enum class TextureType
        {
            ONE_D,
            TWO_D,
            TWO_D_ARRAY,
            THREE_D,
            CUBEMAP,
        } textureType;
        std::vector<std::filesystem::path> inputPaths;
        bool genMipmaps;
        enum class ColorSpace
        {
            LINEAR,
            SRGB,
        } colorSpace;
        uint32_t compressionLevel;
    };

    Recipe loadRecipe(const std::filesystem::path& recipePath)
    {
        Recipe r = {};
        r.outputPath = "res/texture_cooked/" + recipePath.stem().string() + ".hdelicious";

        std::ifstream infile(recipePath);
        std::string line;
        size_t stage = 0;
        size_t numImagesToLoad = 0;
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
                    if (line != "Hawsoo texture RECIPE for delicious consumption")
                    {
                        std::cerr << "ERROR: File tag missing." << std::endl;
                        return Recipe{};
                    }
                    stage++;
                    break;

                case 1:
                    if (line == "1D")
                        r.textureType = Recipe::TextureType::ONE_D;
                    else if (line == "2D")
                        r.textureType = Recipe::TextureType::TWO_D;
                    else if (line == "2DArray")
                        r.textureType = Recipe::TextureType::TWO_D_ARRAY;
                    else if (line == "3D")
                        r.textureType = Recipe::TextureType::THREE_D;
                    else if (line == "Cubemap")
                        r.textureType = Recipe::TextureType::CUBEMAP;
                    else
                        return Recipe{};
                    stage++;
                    break;

                case 2:
                    numImagesToLoad = std::stoi(line);
                    stage++;
                    break;

                case 3:
                    r.inputPaths.push_back("res/texture_pool/" + line);
                    if (--numImagesToLoad <= 0)
                        stage++;
                    break;

                case 4:
                    if (line == "true")
                        r.genMipmaps = true;
                    else if (line == "false")
                        r.genMipmaps = false;
                    else
                        return Recipe{};
                    stage++;
                    break;

                case 5:
                    if (line == "linear")
                        r.colorSpace = Recipe::ColorSpace::LINEAR;
                    else if (line == "srgb")
                        r.colorSpace = Recipe::ColorSpace::SRGB;
                    else
                        return Recipe{};
                    stage++;
                    break;

                case 6:
                    r.compressionLevel = std::stoi(line);
                    stage++;
                    break;
            }
        }

        r.loaded = true;
        return r;
    }

    bool checkTextureCookNeeded(const std::filesystem::path& recipePath)
    {
        Recipe r = loadRecipe(recipePath);
        if (!r.loaded)
        {
            std::cerr << "[CHECK TEXTURE COOK NEEDED]" << std::endl
                << "ERROR: Recipe " << recipePath << " is invalid" << std::endl;
            return false;
        }

        if (!std::filesystem::exists(r.outputPath))
            return true;

        auto lwt = std::filesystem::last_write_time(r.outputPath);
        if (lwt <= std::filesystem::last_write_time(recipePath))
            return true;
        for (auto& inpath : r.inputPaths)
            if (lwt <= std::filesystem::last_write_time(inpath))
                return true;
        return false;
    }

    bool cookTextureFromRecipe(const std::filesystem::path& recipePath)
    {
        Recipe r = loadRecipe(recipePath);
        if (!r.loaded)
        {
            std::cerr << "[COOK TEXTURE FROM RECIPE]" << std::endl
                << "ERROR: Recipe " << recipePath << " is invalid" << std::endl;
            return false;
        }

        int32_t numChannels;
        {
            int32_t w, h;
            stbi_info(r.inputPaths[0].string().c_str(), &w, &h, &numChannels);
        }

        const static std::string toolPath = std::filesystem::absolute("../helper_tools/toktx.exe").string();

        std::string typeArg = "";
        switch (r.textureType)
        {
            case Recipe::TextureType::ONE_D:
                break;

            case Recipe::TextureType::TWO_D:
                typeArg = "--2d";
                break;
            
            case Recipe::TextureType::TWO_D_ARRAY:
                typeArg = "--layers " + std::to_string(r.inputPaths.size());
                break;

            case Recipe::TextureType::THREE_D:
                typeArg = "--depth " + std::to_string(r.inputPaths.size());
                break;

            case Recipe::TextureType::CUBEMAP:
                typeArg = "--cubemap";
                break;
        }

        std::string genMipmapArg = "";
        if (r.genMipmaps)
            genMipmapArg = "--genmipmap";

        // @NOTE: Gaming GPUs support (generally... circa 2023)
        //          - R8_SRGB/_UNORM
        //          - R8G8_UNORM
        //          - R8G8B8A8_SRGB/_UNORM
        //
        //        Therefore, push the target output type to these things:
        //          - R -> R
        //          - RG -> RG if unorm, RGBA if srgb
        //          - RGB -> RGBA
        //          - RGBA -> RGBA
        std::string fourChannelArg = "";
        switch (numChannels)
        {
            case 1:
                fourChannelArg = "--target_type R";
                break;

            case 2:
                if (r.colorSpace == Recipe::ColorSpace::LINEAR)
                    fourChannelArg = "--target_type RG";
                else
                    fourChannelArg = "--target_type RGBA";
                break;

            case 3:
            case 4:
                fourChannelArg = "--target_type RGBA";
                break;
        }

        std::string colorSpaceArg = "";
        switch (r.colorSpace)
        {
            case Recipe::ColorSpace::LINEAR:
                colorSpaceArg = "--assign_oetf linear";
                break;

            case Recipe::ColorSpace::SRGB:
                colorSpaceArg = "--assign_oetf srgb";
                break;
        }

        std::string compressionArg = "";
        if (r.compressionLevel > 0)
            compressionArg = "--zcmp " + std::to_string(r.compressionLevel);

        std::string inputPaths = "";
        for (size_t i = 0; i < r.inputPaths.size(); i++)
        {
            if (i > 0)
                inputPaths += " ";
            inputPaths += r.inputPaths[i].string();
        }

        // Execute script with set of arguments.
        std::cout << "[COOKING TEXTURE]" << std::endl << recipePath.filename() << " to " << r.outputPath.filename() << "\t...\t";
        std::string script =
            toolPath + " " +
            typeArg + " " +
            genMipmapArg + " " +
            fourChannelArg + " " +
            colorSpaceArg + " " +
            compressionArg + " " +
            r.outputPath.string() + " " +
            inputPaths;
        int returnBit = system(script.c_str());  // @NOTE: errors and output get routed to the console anyways! Yay!
        if (returnBit != 0)
        {
            std::cout << "FAILURE" << std::endl
                << "\tCOMMAND: " << script << std::endl;
            return false;
        }

        std::cout << "SUCCESS" << std::endl;
        return true;
    }
}