#pragma once


namespace rng
{
    float_t randomReal();
    float_t randomRealRange(float_t min, float_t max);
    int32_t randomIntegerRange(int32_t min, int32_t max);
    void shuffleVectorSizeType(std::vector<size_t>& inoutVector);  // This should probably be a template function.
}
