#pragma once

#include "ImportGLM.h"


namespace physutil
{
    float_t smoothStep(float_t edge0, float_t edge1, float_t t);
	float_t moveTowards(float_t current, float_t target, float_t maxDistanceDelta);
	glm::i64 moveTowards(glm::i64 current, glm::i64 target, glm::i64 maxDistanceDelta);
	float_t moveTowardsAngle(float_t currentAngle, float_t targetAngle, float_t maxTurnDelta);
	glm::vec2 moveTowardsVec2(glm::vec2 current, glm::vec2 target, float_t maxDistanceDelta);
	glm::vec3 moveTowardsVec3(glm::vec3 current, glm::vec3 target, float_t maxDistanceDelta);
	glm::vec3 clampVector(glm::vec3 vector, float_t min, float_t max);
    glm::vec3 getPosition(const glm::mat4& transform);
	glm::quat getRotation(const glm::mat4& transform);
	glm::vec3 getScale(const glm::mat4& transform);
    float_t lerp(const float_t& a, const float_t& b, const float_t& t);
    glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, const glm::vec3& t);
    bool matrixEquals(const glm::mat4& m1, const glm::mat4& m2, float epsilon = 0.0001f);
}
