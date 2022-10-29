#include "SceneManagement.h"

#include <fstream>
#include <sstream>
#include "DataSerialization.h"

#include "Player.h"


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


const std::string Player::TYPE_NAME = ":player";


bool spinupNewObject(const std::string& objectName, VulkanEngine* engine, DataSerialized* ds)
{
    if (objectName == Player::TYPE_NAME)
        new Player(engine, ds);

    return true;
}


namespace scene
{
    bool loadScene(const std::string& fname, VulkanEngine* engine)
    {
        bool success = true;

        DataSerializer ds;
        std::string newObjectType = "";

        std::ifstream infile(fname);
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
                    success &= spinupNewObject(newObjectType, engine, &dsCooked);
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
                    << "ERROR (line " << lineNum << ") (file: " << fname << "): Headless data" << std::endl
                    << "   Trimmed line: " << line << std::endl
                    << "  Original line: " << line << std::endl;
            }
        }
        
        // @COPYPASTA: Wrap up the previous object if there was one
        if (!newObjectType.empty())
        {
            auto dsCooked = ds.getSerializedData();
            success &= spinupNewObject(newObjectType, engine, &dsCooked);
        }

        return success;
    }

    bool saveScene(const std::string& fname, const std::vector<Entity*>& entities)
    {
        std::ofstream outfile(fname);
        if (!outfile.is_open())
            return false;

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

        return true;
    }
}
