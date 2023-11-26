#include "pch.h"

#include "DataSerialization.h"


void DataSerialized::loadString(std::string& out)
{
    out = _serializedValues[0];
    _serializedValues.erase(_serializedValues.begin());
}

void DataSerialized::loadFloat(float_t& out)
{
    out = std::stof(_serializedValues[0]);
    _serializedValues.erase(_serializedValues.begin());
}

void DataSerialized::loadVec2(vec2& out)
{
    std::string::size_type sz;
    out[0] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    out[1] = std::stof(_serializedValues[0]);
    _serializedValues.erase(_serializedValues.begin());
}

void DataSerialized::loadVec3(vec3& out)
{
    std::string::size_type sz;
    out[0] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    out[1] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    out[2] = std::stof(_serializedValues[0]);
    _serializedValues.erase(_serializedValues.begin());
}

void DataSerialized::loadQuat(versor& out)
{
    std::string::size_type sz;
    out[0] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    out[1] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    out[2] = std::stof(_serializedValues[0], &sz);    _serializedValues[0] = _serializedValues[0].substr(sz);
    out[3] = std::stof(_serializedValues[0]);
    _serializedValues.erase(_serializedValues.begin());
}

void DataSerialized::loadMat4(mat4& out)
{
    std::string::size_type sz;
    float_t* dataPtr = (float_t*)out;  // Dmitri
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
}


size_t DataSerialized::getSerializedValuesCount()
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

void DataSerializer::dumpVec2(const vec2& val)
{
    std::stringstream ss;
    ss << std::to_string(val[0]) << " "
        << std::to_string(val[1]);
    _dataSerialized._serializedValues.push_back(ss.str());
}

void DataSerializer::dumpVec3(vec3 val)
{
    std::stringstream ss;
    ss << std::to_string(val[0]) << " "
        << std::to_string(val[1]) << " "
        << std::to_string(val[2]);
    _dataSerialized._serializedValues.push_back(ss.str());
}

void DataSerializer::dumpQuat(const versor& val)
{
    std::stringstream ss;
    ss << std::to_string(val[0]) << " "
        << std::to_string(val[1]) << " "
        << std::to_string(val[2]) << " "
        << std::to_string(val[3]);
    _dataSerialized._serializedValues.push_back(ss.str());
}

void DataSerializer::dumpMat4(const mat4& val)
{
    std::stringstream ss;
    const float_t* valPtr = (float_t*)val;
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
