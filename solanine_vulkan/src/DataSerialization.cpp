#include "DataSerialization.h"


std::string DataSerialized::loadString()
{
    std::string data = _serializedValues[0];
    _serializedValues.erase(_serializedValues.begin());
    return data;
}

float_t     DataSerialized::loadFloat()
{
    float_t data = std::stof(_serializedValues[0]);
    _serializedValues.erase(_serializedValues.begin());
    return data;
}

glm::vec2   DataSerialized::loadVec2()
{
    std::string::size_type sz;
    glm::vec2 data = {
        std::stof(_serializedValues[0], &sz),
        std::stof(_serializedValues[0].substr(sz))
    };
    _serializedValues.erase(_serializedValues.begin());
    return data;
}

glm::vec3   DataSerialized::loadVec3()
{
    std::string::size_type sz;
    glm::vec3 data = {
        std::stof(_serializedValues[0], &sz),
        std::stof(_serializedValues[0].substr(sz), &sz),
        std::stof(_serializedValues[0].substr(sz))
    };
    _serializedValues.erase(_serializedValues.begin());
    return data;
}


size_t      DataSerialized::getSerializedValuesCount()
{
    return _serializedValues.size();
}


void DataSerializer::dumpString(const std::string& val)
{
    _dataSerialized._serializedValues.push_back(val);
}

void DataSerializer::dumpFloat(const float_t& val)
{
    std::stringstream ss;
    ss << std::to_string(val);
    _dataSerialized._serializedValues.push_back(ss.str());
}

void DataSerializer::dumpVec2(const glm::vec2& val)
{
    std::stringstream ss;
    ss << std::to_string(val.x) << " " << std::to_string(val.y);
    _dataSerialized._serializedValues.push_back(ss.str());
}

void DataSerializer::dumpVec3(const glm::vec3& val)
{
    std::stringstream ss;
    ss << std::to_string(val.x) << " " << std::to_string(val.y) << " " << std::to_string(val.z);
    _dataSerialized._serializedValues.push_back(ss.str());
}

DataSerialized DataSerializer::getSerializedData()
{
    return _dataSerialized;
}
