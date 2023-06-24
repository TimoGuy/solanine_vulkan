#include "SceneManagement.h"

#include <fstream>
#include <sstream>
#include "DataSerialization.h"
#include "VulkanEngine.h"
#include "Camera.h"
#include "Debug.h"
#include "StringHelper.h"
#include "GlobalState.h"

#include "Player.h"
#include "NoteTaker.h"
#include "VoxelField.h"
#include "ScannableItem.h"
#include "HarvestableItem.h"
#include "Beanbag.h"


// @PALETTE: where to add serialized names for the entities
const std::vector<std::string> ENTITY_TYPE_NAMES = {
    ":player",
    ":notetaker",
    ":voxelfield",
    ":scannableitem",
    ":harvestableitem",
    ":beanbag",
};
const std::string Player::TYPE_NAME           = ENTITY_TYPE_NAMES[0];
const std::string NoteTaker::TYPE_NAME        = ENTITY_TYPE_NAMES[1];
const std::string VoxelField::TYPE_NAME       = ENTITY_TYPE_NAMES[2];
const std::string ScannableItem::TYPE_NAME    = ENTITY_TYPE_NAMES[3];
const std::string HarvestableItem::TYPE_NAME  = ENTITY_TYPE_NAMES[4];
const std::string Beanbag::TYPE_NAME          = ENTITY_TYPE_NAMES[5];


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
            ent = new Player(engine->_entityManager, engine->_roManager, engine->_camera, ds);
        if (objectName == NoteTaker::TYPE_NAME)
            ent = new NoteTaker(engine->_entityManager, engine->_roManager, ds);
        if (objectName == VoxelField::TYPE_NAME)
            ent = new VoxelField(engine, engine->_entityManager, engine->_roManager, ds);
        if (objectName == ScannableItem::TYPE_NAME)
            ent = new ScannableItem(engine->_entityManager, engine->_roManager, ds);
        if (objectName == HarvestableItem::TYPE_NAME)
            ent = new HarvestableItem(engine->_entityManager, engine->_roManager, ds);
        if (objectName == Beanbag::TYPE_NAME)
            ent = new Beanbag(engine->_entityManager, engine->_roManager, ds);

        if (ent == nullptr)
        {
            std::cerr << "[ENTITY CREATION]" << std::endl
                << "ERROR: creating entity \"" << objectName << "\" did not match any creation routines." << std::endl;
        }

        return ent;
    }

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

        globalState::savedActiveScene = name;

        if (success)
            debug::pushDebugMessage({
			    .message = "Successfully loaded scene \"" + name + "\"",
			    });
        else
            debug::pushDebugMessage({
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
            debug::pushDebugMessage({
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
            {
                std::string s;
                dsd.loadString(s);
                outfile << s << '\n';
            }

            outfile << '\n';  // Extra newline for readability
        }
        outfile.close();

        debug::pushDebugMessage({
			.message = "Successfully saved scene \"" + name + "\"",
			});

        return true;
    }
}
