#pragma once

#include "Imports.h"


class DataSerialized
{
public:
    void loadString(std::string& out);
    void loadFloat(float_t& out);
    void loadVec2(vec2& out);
    void loadVec3(vec3& out);
    void loadQuat(versor& out);
    void loadMat4(mat4& out);
    size_t getSerializedValuesCount();

private:
    DataSerialized() = default;
    std::vector<std::string> _serializedValues;

    friend class DataSerializer;
};


class DataSerializer
{
public:
    DataSerializer() = default;
    void           dumpString(const std::string& val);
    void           dumpFloat(const float_t& val);
    void           dumpVec2(const vec2& val);
    void           dumpVec3(const vec3& val);
    void           dumpQuat(const versor& val);
    void           dumpMat4(const mat4& val);
    DataSerialized getSerializedData();

private:
    DataSerialized _dataSerialized;
};
