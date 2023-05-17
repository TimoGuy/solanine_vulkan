#pragma once

#include "Imports.h"


class DataSerialized
{
public:
    std::string loadString();
    float_t     loadFloat();
    glm::vec2   loadVec2();
    vec3   loadVec3();
    glm::quat   loadQuat();
    mat4   loadMat4();
    size_t      getSerializedValuesCount();

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
    void           dumpVec2(const glm::vec2& val);
    void           dumpVec3(const vec3& val);
    void           dumpQuat(const glm::quat& val);
    void           dumpMat4(const mat4& val);
    DataSerialized getSerializedData();

private:
    DataSerialized _dataSerialized;
};
