#pragma once

#include "Imports.h"


class DataSerialized
{
public:
    std::string loadString();
    float_t     loadFloat();
    glm::vec2   loadVec2();
    glm::vec3   loadVec3();
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
    void           dumpVec3(const glm::vec3& val);
    DataSerialized getSerializedData();

private:
    DataSerialized _dataSerialized;
};
