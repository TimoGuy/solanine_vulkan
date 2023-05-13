#include "PhysUtil.h"

#include <algorithm>


namespace physutil
{
	float_t smoothStep(float_t edge0, float_t edge1, float_t t)
	{
		t = std::clamp((t - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3 - 2 * t);
	}

	float_t moveTowards(float_t current, float_t target, float_t maxDistanceDelta)
	{
		float_t delta = target - current;
		return (maxDistanceDelta >= std::abs(delta)) ? target : (current + std::copysignf(1.0f, delta) * maxDistanceDelta);
	}

	glm::i64 moveTowards(glm::i64 current, glm::i64 target, glm::i64 maxDistanceDelta)
	{
		glm::i64 delta = target - current;
		return (maxDistanceDelta >= glm::abs(delta)) ? target : (current + glm::sign(delta) * maxDistanceDelta);
	}

	float_t moveTowardsAngle(float_t currentAngle, float_t targetAngle, float_t maxTurnDelta)
	{
		float_t result;
		float_t diff = targetAngle - currentAngle;
		if (diff < -180.0f)
		{
			// Move upwards past 360
			targetAngle += 360.0f;
			result = moveTowards(currentAngle, targetAngle, maxTurnDelta);
			if (result >= 360.0f)
			{
				result -= 360.0f;
			}
		}
		else if (diff > 180.0f)
		{
			// Move downwards past 0
			targetAngle -= 360.0f;
			result = moveTowards(currentAngle, targetAngle, maxTurnDelta);
			if (result < 0.0f)
			{
				result += 360.0f;
			}
		}
		else
		{
			// Straight move
			result = moveTowards(currentAngle, targetAngle, maxTurnDelta);
		}

		return result;
	}

	glm::vec2 moveTowardsVec2(glm::vec2 current, glm::vec2 target, float_t maxDistanceDelta)
	{
		float_t delta = glm::length(target - current);
		glm::vec2 mvtDeltaNormalized = glm::normalize(target - current);
		return (maxDistanceDelta >= std::abs(delta)) ? target : (current + mvtDeltaNormalized * maxDistanceDelta);
	}

	glm::vec3 moveTowardsVec3(glm::vec3 current, glm::vec3 target, float_t maxDistanceDelta)
	{
		float_t delta = glm::length(target - current);
		glm::vec3 mvtDeltaNormalized = glm::normalize(target - current);
		return (maxDistanceDelta >= std::abs(delta)) ? target : (current + mvtDeltaNormalized * maxDistanceDelta);
	}

	glm::vec3 clampVector(glm::vec3 vector, float_t min, float_t max)
	{
		float_t magnitude = glm::length(vector);

		assert(std::abs(magnitude) > 0.00001f);

		return glm::normalize(vector) * std::clamp(magnitude, min, max);
	}

	glm::vec3 getPosition(const glm::mat4& transform)
	{
		return glm::vec3(transform[3]);
	}

	glm::quat getRotation(const glm::mat4& transform)
	{
		// NOTE: when the scale gets larger, the quaternion will rotate up to however many dimensions there are, thus we have to scale down/normalize this transform to unit scale before extracting the quaternion
		glm::vec3 scale = getScale(transform);
		const glm::mat3 unitScaledRotationMatrix(
			glm::vec3(transform[0]) / scale[0],
			glm::vec3(transform[1]) / scale[1],
			glm::vec3(transform[2]) / scale[2]
		);
		return glm::normalize(glm::quat_cast(unitScaledRotationMatrix));		// NOTE: Seems like the quat created here needs to be normalized. Weird.  -Timo 2022-01-19
	}

	glm::vec3 getScale(const glm::mat4& transform)
	{
		glm::vec3 scale = {
			glm::length(glm::vec3(transform[0])),
			glm::length(glm::vec3(transform[1])),
			glm::length(glm::vec3(transform[2])),
		};
		return scale;
	}

	float_t lerp(const float_t& a, const float_t& b, const float_t& t)
	{
		return ((1.0f - t) * a) + (t * b);
	}

	glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, const glm::vec3& t)
	{
		return glm::vec3(lerp(a.x, b.x, t.x), lerp(a.y, b.y, t.y), lerp(a.z, b.z, t.z));
	}

	bool matrixEquals(const glm::mat4& m1, const glm::mat4& m2, float epsilon)
	{
		return (glm::abs(m1[0][0] - m2[0][0]) < epsilon &&
			glm::abs(m1[0][1] - m2[0][1]) < epsilon &&
			glm::abs(m1[0][2] - m2[0][2]) < epsilon &&
			glm::abs(m1[0][3] - m2[0][3]) < epsilon &&
			glm::abs(m1[1][0] - m2[1][0]) < epsilon &&
			glm::abs(m1[1][1] - m2[1][1]) < epsilon &&
			glm::abs(m1[1][2] - m2[1][2]) < epsilon &&
			glm::abs(m1[1][3] - m2[1][3]) < epsilon &&
			glm::abs(m1[2][0] - m2[2][0]) < epsilon &&
			glm::abs(m1[2][1] - m2[2][1]) < epsilon &&
			glm::abs(m1[2][2] - m2[2][2]) < epsilon &&
			glm::abs(m1[2][3] - m2[2][3]) < epsilon &&
			glm::abs(m1[3][0] - m2[3][0]) < epsilon &&
			glm::abs(m1[3][1] - m2[3][1]) < epsilon &&
			glm::abs(m1[3][2] - m2[3][2]) < epsilon &&
			glm::abs(m1[3][3] - m2[3][3]) < epsilon);
	}
}
