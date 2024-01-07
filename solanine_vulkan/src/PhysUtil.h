#pragma once


namespace physutil
{
	// @NOTE: all of these functions aren't getting used. Either that could be because they're not necessary
	//        (cglm has a lot of helper functions), or they will be used in the future. In case if the answer
	//        is the latter, all these functions are just commented out, keeping their glm form instead of
	//        converting to cglm.  -Timo 2023/05/17

    // float_t smoothStep(float_t edge0, float_t edge1, float_t t);
	float_t moveTowards(float_t current, float_t target, float_t maxDistanceDelta);
	int32_t moveTowards(int32_t current, int32_t target, int32_t maxDistanceDelta);
	// float_t moveTowardsAngle(float_t currentAngle, float_t targetAngle, float_t maxTurnDelta);
	// glm::vec2 moveTowardsVec2(glm::vec2 current, glm::vec2 target, float_t maxDistanceDelta);
	// vec3 moveTowardsVec3(vec3 current, vec3 target, float_t maxDistanceDelta);
	// vec3 clampVector(vec3 vector, float_t min, float_t max);
    // vec3 getPosition(const mat4& transform);
	// glm::quat getRotation(const mat4& transform);
	// vec3 getScale(const mat4& transform);
    float_t lerp(const float_t& a, const float_t& b, const float_t& t);
    // vec3 lerp(vec3 a, vec3 b, vec3 t);
    // bool matrixEquals(const mat4& m1, const mat4& m2, float epsilon = 0.0001f);
}
