#include "TextureCooker.h"

#include "StringHelper.h"


namespace texturecooker
{
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
        enum class EncodingFormat
        {
            NONE,
            UASTC,
            ETC1S,
        } encodingFormat;
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
                        return Recipe{};
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
                    if (line == "none")
                        r.encodingFormat = Recipe::EncodingFormat::NONE;
                    else if (line == "uastc")
                        r.encodingFormat = Recipe::EncodingFormat::UASTC;
                    else if (line == "etc1s")
                    {
                        r.encodingFormat = Recipe::EncodingFormat::ETC1S;
                        stage++;  // Increment `stage` twice to skip over compression level.
                    }
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

        if (!std::filesystem::exists(r.outputPath) ||
            std::filesystem::last_write_time(r.outputPath) <= std::filesystem::last_write_time(recipePath))
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

        std::string encodingArg = "";
        switch (r.encodingFormat)
        {
            case Recipe::EncodingFormat::NONE:
                break;

            case Recipe::EncodingFormat::UASTC:
                encodingArg = "--encode uastc";
                break;

            case Recipe::EncodingFormat::ETC1S:
                encodingArg = "--encode etc1s";
                break;
        }

        std::string compressionArg = "";
        if (r.encodingFormat != Recipe::EncodingFormat::ETC1S && r.compressionLevel > 0)
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
            encodingArg + " " +
            compressionArg + " " +
            r.outputPath.string() + " " +
            inputPaths;
        int returnBit = system(script.c_str());  // @NOTE: errors and output get routed to the console anyways! Yay!
        if (returnBit != 0)
        {
            std::cout << "FAILURE" << std::endl;
            return false;
        }

        std::cout << "SUCCESS" << std::endl;
        return true;
    }
}