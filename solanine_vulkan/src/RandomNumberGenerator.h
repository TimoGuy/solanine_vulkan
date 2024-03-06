#pragma once


namespace rng
{
    float_t randomReal();  // Generates a floating point number [0, 1], inclusive.
    float_t randomRealRange(float_t min, float_t max);  // @NOTE: this is [min, max], so max is inclusive!
    int32_t randomIntegerRange(int32_t min, int32_t max);  // @NOTE: this is [min, max], so max is inclusive!
    void shuffleVectorSizeType(std::vector<size_t>& inoutVector);  // This should probably be a template function.
}
