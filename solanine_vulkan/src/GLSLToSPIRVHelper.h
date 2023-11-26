#pragma once
#ifdef _DEVELOP

namespace glslToSPIRVHelper
{
    bool checkGLSLShaderCompileNeeded(const std::filesystem::path& sourceCodePath);
    bool compileGLSLShaderToSPIRV(const std::filesystem::path& sourceCodePath);
}

#endif