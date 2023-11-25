#pragma once


#ifdef _DEVELOP

#include <iostream>
#include <filesystem>


namespace glslToSPIRVHelper
{
    bool checkGLSLShaderCompileNeeded(const std::filesystem::path& sourceCodePath)
    {
        auto spvPath = sourceCodePath;
        spvPath += ".spv";

        if (!std::filesystem::exists(spvPath) ||
            std::filesystem::last_write_time(spvPath) <= std::filesystem::last_write_time(sourceCodePath))
        {
            return true;
        }
        return false;
    }

    bool compileGLSLShaderToSPIRV(const std::filesystem::path& sourceCodePath)
    {
        std::cout << "[COMPILING SHADER SOURCE]" << std::endl << sourceCodePath << " to SPIRV\t...\t";

        if (!std::filesystem::exists(sourceCodePath))
        {
            std::cerr << "ERROR: shader source file " << sourceCodePath << " does not exist, osoraku" << std::endl;
            std::cout << "FAILURE" << std::endl;
            return false;
        }

        //
        // Compile the file and save the results in a .spv file and the messages in a .log file
        //
        const static std::string compilerPath = std::filesystem::absolute("../helper_tools/glslc.exe").string();
        auto spvPath = sourceCodePath;  spvPath += ".spv";
        int compilerBit = system((compilerPath + " " + sourceCodePath.string() + " -o " + spvPath.string()).c_str());  // @NOTE: errors and output get routed to the console anyways! Yay!

        //
        // Read the .log file if there was an error
        //
        if (compilerBit != 0)
        {
            std::cout << "FAILURE" << std::endl;
            return false;
        }

        std::cout << "SUCCESS" << std::endl;
        return true;
    }
}

#endif