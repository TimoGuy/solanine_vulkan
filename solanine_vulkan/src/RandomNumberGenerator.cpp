#include "RandomNumberGenerator.h"

#include <random>
#include <chrono>


namespace rng
{
    std::default_random_engine generator(std::chrono::system_clock::now().time_since_epoch().count());

    float_t randomReal()
    {
        return randomRealRange(0.0f, 1.0f);
    }

    float_t randomRealRange(float_t min, float_t max)
    {
        float_t temp = std::min(min, max);
        max = std::max(min, max);
        min = temp;
		std::uniform_real_distribution<float_t> distribution(min, max);
        return distribution(generator);
    }

    int32_t randomIntegerRange(int32_t min, int32_t max)
    {
		std::uniform_int_distribution<int32_t> distribution(min, max);
        return distribution(generator);
    }

    void shuffleVectorSizeType(std::vector<size_t>& inoutVector)
    {
        std::shuffle(inoutVector.begin(), inoutVector.end(), generator);
    }
}
