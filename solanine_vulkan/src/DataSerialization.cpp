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
    glm::vec2 data;
    float_t* dataPtr = glm::value_ptr(data);
    dataPtr[0]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[1]  = std::stof(_serializedValues[0]);
    _serializedValues.erase(_serializedValues.begin());
    return data;
}

glm::vec3   DataSerialized::loadVec3()
{
    std::string::size_type sz;
    glm::vec3 data;
    float_t* dataPtr = glm::value_ptr(data);
    dataPtr[0]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[1]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[2]  = std::stof(_serializedValues[0]);
    _serializedValues.erase(_serializedValues.begin());
    return data;
}

glm::mat4   DataSerialized::loadMat4()
{
    std::string::size_type sz;
    glm::mat4 data;
    float_t* dataPtr = glm::value_ptr(data);  // Dmitri
    dataPtr[0]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[1]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[2]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[3]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[4]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[5]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[6]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[7]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[8]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[9]  = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[10] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[11] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[12] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[13] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[14] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    dataPtr[15] = std::stof(_serializedValues[0]);
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
    ss << std::to_string(val.x) << " "
        << std::to_string(val.y);
    _dataSerialized._serializedValues.push_back(ss.str());
}

void DataSerializer::dumpVec3(const glm::vec3& val)
{
    std::stringstream ss;
    ss << std::to_string(val.x) << " "
        << std::to_string(val.y) << " "
        << std::to_string(val.z);
    _dataSerialized._serializedValues.push_back(ss.str());
}

void DataSerializer::dumpMat4(const glm::mat4& val)
{
    std::stringstream ss;
    const float_t* valPtr = glm::value_ptr(val);
    ss << std::to_string(valPtr[0]) << " "
        << std::to_string(valPtr[1]) << " "
        << std::to_string(valPtr[2]) << " "
        << std::to_string(valPtr[3]) << " "
        << std::to_string(valPtr[4]) << " "
        << std::to_string(valPtr[5]) << " "
        << std::to_string(valPtr[6]) << " "
        << std::to_string(valPtr[7]) << " "
        << std::to_string(valPtr[8]) << " "
        << std::to_string(valPtr[9]) << " "
        << std::to_string(valPtr[10]) << " "
        << std::to_string(valPtr[11]) << " "
        << std::to_string(valPtr[12]) << " "
        << std::to_string(valPtr[13]) << " "
        << std::to_string(valPtr[14]) << " "
        << std::to_string(valPtr[15]);
    _dataSerialized._serializedValues.push_back(ss.str());
}

DataSerialized DataSerializer::getSerializedData()
{
    return _dataSerialized;
}
