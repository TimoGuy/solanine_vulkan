#include "pch.h"

#include "SceneManagement.h"

#include "DataSerialization.h"
#include "VulkanEngine.h"
#include "PhysicsEngine.h"
#include "EntityManager.h"
#include "Camera.h"
#include "Debug.h"
#include "StringHelper.h"
#include "GlobalState.h"

#include "SimulationCharacter.h"
#include "NoteTaker.h"
#include "VoxelField.h"
#include "ScannableItem.h"
#include "HarvestableItem.h"
#include "GondolaSystem.h"
#include "EDITORTextureViewer.h"
#include "EDITORTestLevelSpawnPoint.h"


// @PALETTE: where to add serialized names for the entities
struct PaletteElem
{
    std::string name;
    bool showInEntityCreation;
};
const std::vector<PaletteElem> PALETTE_ELEMENTS = {
    { ":character", false },
    { ":notetaker", true },
    { ":voxelfield", true },
    { ":scannableitem", true },
    { ":harvestableitem", true },
    { ":gondolasystem", true },
    { ":EDITORtextureviewer", true },
    { ":EDITORspawnpoint", true },
};
const std::string SimulationCharacter::TYPE_NAME       = PALETTE_ELEMENTS[0].name;
const std::string NoteTaker::TYPE_NAME                 = PALETTE_ELEMENTS[1].name;
const std::string VoxelField::TYPE_NAME                = PALETTE_ELEMENTS[2].name;
const std::string ScannableItem::TYPE_NAME             = PALETTE_ELEMENTS[3].name;
const std::string HarvestableItem::TYPE_NAME           = PALETTE_ELEMENTS[4].name;
const std::string GondolaSystem::TYPE_NAME             = PALETTE_ELEMENTS[5].name;
const std::string EDITORTextureViewer::TYPE_NAME       = PALETTE_ELEMENTS[6].name;
const std::string EDITORTestLevelSpawnPoint::TYPE_NAME = PALETTE_ELEMENTS[7].name;


namespace scene
{
    VulkanEngine* engine;
    bool performingDeleteAllLoadSceneProcedure;
    bool performingLoadSceneImmediateLoadSceneProcedure;
    std::string performingLoadSceneImmediateLoadSceneProcedureSavedSceneName;

    void init(VulkanEngine* inEnginePtr)
    {
        engine = inEnginePtr;
        performingDeleteAllLoadSceneProcedure = false;
        performingLoadSceneImmediateLoadSceneProcedure = false;
        performingLoadSceneImmediateLoadSceneProcedureSavedSceneName = "";
    }

    bool loadSceneImmediate(const std::string& name);
    void tick()
    {
        if (performingDeleteAllLoadSceneProcedure)
        {
            // Delete all entities.
            for (auto& ent : engine->_entityManager->_entities)
                engine->_entityManager->destroyEntity(ent);

            performingDeleteAllLoadSceneProcedure = false;
            performingLoadSceneImmediateLoadSceneProcedure = true;
        }
        else if (performingLoadSceneImmediateLoadSceneProcedure)
        {
            loadSceneImmediate(performingLoadSceneImmediateLoadSceneProcedureSavedSceneName);

            performingDeleteAllLoadSceneProcedure = false;
            performingLoadSceneImmediateLoadSceneProcedure = false;
            performingLoadSceneImmediateLoadSceneProcedureSavedSceneName = "";
        }
    }

    std::vector<std::string> getListOfEntityTypes()
    {
        std::vector<std::string> names;
        for (auto& elem : PALETTE_ELEMENTS)
            if (elem.showInEntityCreation)
                names.push_back(elem.name);
        return names;
    }

    Entity* spinupNewObject(const std::string& objectName, DataSerialized* ds)
    {
        Entity* ent = nullptr;
        if (objectName == SimulationCharacter::TYPE_NAME)
            ent = new SimulationCharacter(engine->_entityManager, engine->_roManager, engine->_camera, ds);
        if (objectName == NoteTaker::TYPE_NAME)
            ent = new NoteTaker(engine->_entityManager, engine->_roManager, ds);
        if (objectName == VoxelField::TYPE_NAME)
            ent = new VoxelField(engine, engine->_entityManager, engine->_roManager, ds);
        if (objectName == ScannableItem::TYPE_NAME)
            ent = new ScannableItem(engine->_entityManager, engine->_roManager, ds);
        if (objectName == HarvestableItem::TYPE_NAME)
            ent = new HarvestableItem(engine->_entityManager, engine->_roManager, ds);
        if (objectName == GondolaSystem::TYPE_NAME)
            ent = new GondolaSystem(engine->_entityManager, engine->_roManager, ds);
        if (objectName == EDITORTextureViewer::TYPE_NAME)
            ent = new EDITORTextureViewer(engine->_entityManager, engine->_roManager, ds);
        if (objectName == EDITORTestLevelSpawnPoint::TYPE_NAME)
            ent = new EDITORTestLevelSpawnPoint(engine->_entityManager, engine->_roManager, ds);

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
            if (!path.has_extension() || path.extension().compare(".hentais") != 0)
                continue;
            auto relativePath = std::filesystem::relative(path, SCENE_DIRECTORY_PATH);
            scenes.push_back(relativePath.string());  // @NOTE: that this line could be dangerous if there are any filenames or directory names that have utf8 chars or wchars in it
        }
        return scenes;
    }

    std::vector<std::string> getListOfPrefabs()
    {
        std::vector<std::string> prefabs;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(PREFAB_DIRECTORY_PATH))
        {
            const auto& path = entry.path();
            if (std::filesystem::is_directory(path))
                continue;
            if (!path.has_extension() || path.extension().compare(".hunk") != 0)
                continue;
            auto relativePath = std::filesystem::relative(path, PREFAB_DIRECTORY_PATH);
            prefabs.push_back(relativePath.string());  // @NOTE: that this line could be dangerous if there are any filenames or directory names that have utf8 chars or wchars in it
        }
        return prefabs;
    }

    bool loadSerializationFull(const std::string& fullFname, const std::string& fileTag, bool ownEntities, std::vector<Entity*>& outEntityPtrs)
    {
        bool success = true;

        DataSerializer ds;
        std::string newObjectType = "";

        bool foundTag = false;
        std::ifstream infile(fullFname);
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
            if (!foundTag)
            {
                if (line == fileTag)
                {
                    foundTag = true;
                }
                else
                {
                    // File type is discerned to be incorrect.
                    std::cerr << "[SCENE MANAGEMENT]" << std::endl
                        << "ERROR: File must start with proper file marker. File is discerned to be corrupt. Abort." << std::endl;
                    return false;
                }
            }
            else if (line[0] == ':')
            {
                // Wrap up the previous object if there was one
                if (!newObjectType.empty())
                {
                    auto dsCooked = ds.getSerializedData();
                    Entity* newEntity = spinupNewObject(newObjectType, &dsCooked);
                    newEntity->_isOwned = ownEntities;
                    outEntityPtrs.push_back(newEntity);
                    success &= (newEntity != nullptr);
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
                    << "ERROR (line " << lineNum << ") (file: " << SCENE_DIRECTORY_PATH << fullFname << "): Headless data" << std::endl
                    << "   Trimmed line: " << line << std::endl
                    << "  Original line: " << line << std::endl;
            }
        }
        
        // @COPYPASTA: Wrap up the previous object if there was one
        if (!newObjectType.empty())
        {
            auto dsCooked = ds.getSerializedData();
            Entity* newEntity = spinupNewObject(newObjectType, &dsCooked);
            outEntityPtrs.push_back(newEntity);
            success &= (newEntity != nullptr);
        }

        return success;
    }

    constexpr const char* FILE_PREFAB_TAG = "Hawsoo prefab UNK";
    constexpr const char* FILE_SCENE_TAG = "Hawsoo ENTity Assortment of IdentitieS";

    bool loadPrefab(const std::string& name, std::vector<Entity*>& outEntityPtrs)
    {
        return loadSerializationFull(PREFAB_DIRECTORY_PATH + name, std::string(FILE_PREFAB_TAG), true, outEntityPtrs);
    }

    bool loadPrefabNonOwned(const std::string& name)
    {
        std::vector<Entity*> _;
        return loadSerializationFull(PREFAB_DIRECTORY_PATH + name, std::string(FILE_PREFAB_TAG), false, _);
    }
    
    bool loadScene(const std::string& name, bool deleteExistingEntitiesFirst)
    {
        if (deleteExistingEntitiesFirst)
        {
            performingDeleteAllLoadSceneProcedure = true;
            performingLoadSceneImmediateLoadSceneProcedureSavedSceneName = name;
            return true;
        }
        else
            return loadSceneImmediate(name);
    }

    bool loadSceneImmediate(const std::string& name)
    {
        std::vector<Entity*> _;
        bool ret = loadSerializationFull(SCENE_DIRECTORY_PATH + name, std::string(FILE_SCENE_TAG), false, _);

        if (ret)
            debug::pushDebugMessage({
			    .message = "Successfully loaded scene \"" + name + "\"",
			    });
        else
            debug::pushDebugMessage({
                .message = "Loaded scene \"" + name + "\" with errors (see console output)",
                .type = 1,
                });

        // @DEBUG: save snapshot of physics frame.
        physengine::savePhysicsWorldSnapshot();

        return true;
    }

    bool saveScene(const std::string& name, const std::vector<Entity*>& entities)
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

        outfile << "Hawsoo ENTity Assortment of IdentitieS" << '\n' << '\n';  // File marker.

        for (auto ent : entities)
        {
            if (ent->_isOwned)
                continue;  // Don't save owned entities.

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
