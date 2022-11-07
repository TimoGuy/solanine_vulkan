#include "GenerateGUID.h"

#include "Imports.h"


uint32_t randomChar()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, 255);
	return dis(gen);
}

std::string generateGUID()
{
	constexpr uint32_t numChars = 32;
	std::stringstream ss;
	for (uint32_t i = 0; i < numChars; i++)
	{
		const auto rc = randomChar();
		std::stringstream hexstream;
		hexstream << std::hex << rc;
		auto hex = hexstream.str();
		ss << (hex.length() < 2 ? '0' + hex : hex);
	}
	return ss.str();
}
