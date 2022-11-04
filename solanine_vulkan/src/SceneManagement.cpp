#include "SceneManagement.h"

#include <fstream>
#include <sstream>
#include "DataSerialization.h"
//#include "VulkanEgneinfd.h"

#include "Player.h"
#include "Yosemite.h"


// @NOTE: copied from https://stackoverflow.com/questions/216823/how-to-trim-an-stdstring
// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}


const std::vector<std::string> ENTITY_TYPE_NAMES = {
    ":player",
    ":yosemite",
};
const std::string Player::TYPE_NAME   = ENTITY_TYPE_NAMES[0];
const std::string Yosemite::TYPE_NAME = ENTITY_TYPE_NAMES[1];


namespace scene
{
    std::vector<std::string> getListOfEntityTypes()
    {
        return ENTITY_TYPE_NAMES;
    }

    Entity* spinupNewObject(const std::string& objectName, VulkanEngine* engine, DataSerialized* ds)
    {
        Entity* ent = nullptr;
        if (objectName == Player::TYPE_NAME)
            ent = new Player(engine, ds);
        if (objectName == Yosemite::TYPE_NAME)
            ent = new Yosemite(engine, ds);

        return ent;
    }

    extern std::string currentLoadedScene = "";

    std::vector<std::string> getListOfScenes()
    {
        std::vector<std::string> scenes;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(SCENE_DIRECTORY_PATH))
        {
            const auto& path = entry.path();
            if (std::filesystem::is_directory(path))
                continue;
            if (!path.has_extension() || path.extension().compare(".ssdat") != 0)
                continue;
            auto relativePath = std::filesystem::relative(path, SCENE_DIRECTORY_PATH);
            scenes.push_back(relativePath.string());  // @NOTE: that this line could be dangerous if there are any filenames or directory names that have utf8 chars or wchars in it
        }
        return scenes;
    }
    
    bool loadScene(const std::string& name, VulkanEngine* engine)
    {
        bool success = true;

        DataSerializer ds;
        std::string newObjectType = "";

        std::ifstream infile(SCENE_DIRECTORY_PATH + name);
        std::string line;
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

            //
            // Process that line
            //
            if (line[0] == ':')
            {
                // Wrap up the previous object if there was one
                if (!newObjectType.empty())
                {
                    auto dsCooked = ds.getSerializedData();
                    success &= (spinupNewObject(newObjectType, engine, &dsCooked) != nullptr);
                }

                // New object
                ds = DataSerializer();
                newObjectType = line;
            }
            else if (line[0] == '~')
            {
                // @INCOMPLETE: still need to implement object count and possibly multithreading?
                //              Note that the object count would be needed to create a buffer to
                //              have the threads write to to avoid any data races when push_back()
                //              onto a vector  -Timo 2022/10/29
            }
            else if (!newObjectType.empty())
            {
                // Concat data to the object
                ds.dumpString(line);
            }
            else
            {
                // ERROR
                std::cerr << "[SCENE MANAGEMENT]" << std::endl
                    << "ERROR (line " << lineNum << ") (file: " << SCENE_DIRECTORY_PATH << name << "): Headless data" << std::endl
                    << "   Trimmed line: " << line << std::endl
                    << "  Original line: " << line << std::endl;
            }
        }
        
        // @COPYPASTA: Wrap up the previous object if there was one
        if (!newObjectType.empty())
        {
            auto dsCooked = ds.getSerializedData();
            success &= (spinupNewObject(newObjectType, engine, &dsCooked) != nullptr);
        }

        currentLoadedScene = name;

        if (success)
            engine->pushDebugMessage({
			    .message = "Successfully loaded scene \"" + name + "\"",
			    });
        else
            engine->pushDebugMessage({
                .message = "Loaded scene \"" + name + "\" with errors (see console output)",
                .type = 1,
                });

        return success;
    }

    bool saveScene(const std::string& name, const std::vector<Entity*>& entities, VulkanEngine* engine)
    {
        std::ofstream outfile(SCENE_DIRECTORY_PATH + name);
        if (!outfile.is_open())
        {
            engine->pushDebugMessage({
			    .message = "Could not open file \"" + name + "\" for writing scene data",
                .type = 2,
			    });
            return false;
        }

        for (auto ent : entities)
        {
            DataSerializer ds;
            ent->dump(ds);

            outfile << ent->getTypeName() << '\n';

            DataSerialized dsd = ds.getSerializedData();
            size_t count = dsd.getSerializedValuesCount();
            for (size_t i = 0; i < count; i++)
                outfile << dsd.loadString() << '\n';

            outfile << '\n';  // Extra newline for readability
        }
        outfile.close();

        engine->pushDebugMessage({
			.message = "Successfully saved scene \"" + name + "\"",
			});

        return true;
    }
}
