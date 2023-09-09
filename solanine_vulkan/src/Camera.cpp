#include "Camera.h"

#include <string>
#include <SDL2/SDL.h>
#include "PhysUtil.h"
#include "ImportGLM.h"
#include "InputManager.h"
#include "RenderObject.h"
#include "PhysicsEngine.h"
#include "VulkanEngine.h"  // VulkanEngine
#include "Debug.h"


//
// SceneCamera
//
void SceneCamera::recalculateSceneCamera(GPUPBRShadingProps& pbrShadingProps)
{
	vec3 center;
	glm_vec3_add(gpuCameraData.cameraPosition, facingDirection, center);
	vec3 up = { 0.0f, 1.0f, 0.0f };
	mat4 view;
	glm_lookat(gpuCameraData.cameraPosition, center, up, view);
	mat4 projection;
	glm_perspective(fov, aspect, zNear, zFar, projection);
	projection[1][1] *= -1.0f;
	glm_mat4_copy(view, gpuCameraData.view);
	glm_mat4_copy(projection, gpuCameraData.projection);
	glm_mat4_mul(projection, view, gpuCameraData.projectionView);

	recalculateCascadeViewProjs(pbrShadingProps);
}

void SceneCamera::recalculateCascadeViewProjs(GPUPBRShadingProps& pbrShadingProps)
{
	// Copied from Sascha Willem's `shadowmappingcascade` vulkan example
	constexpr float_t cascadeSplitLambda = 0.95f;  // @TEMP: don't know if this needs tuning

	float_t cascadeSplits[SHADOWMAP_CASCADES];

	const float_t& nearClip = zNear;
	const float_t& farClip = zFarShadow;
	const float_t clipRange = farClip - nearClip;

	const float_t minZ = nearClip;
	const float_t maxZ = nearClip + clipRange;

	const float_t range = maxZ - minZ;
	const float_t ratio = maxZ / minZ;

	// Calculate split depths based on view camera frustum
	// Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
	for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
	{
		float_t p = (i + 1) / static_cast<float_t>(SHADOWMAP_CASCADES);
		float_t log = minZ * std::pow(ratio, p);
		float_t uniform = minZ + range * p;
		float_t d = cascadeSplitLambda * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearClip) / clipRange;
	}

	// Calculate orthographic projection matrix for each cascade
	vec3 up = { 0.0f, 1.0f, 0.0f };
	float_t lastSplitDist = 0.0;
	for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
	{
		float_t splitDist = cascadeSplits[i];

		vec3 frustumCorners[8] = {
			{ -1.0f,  1.0f, -1.0f },
			{  1.0f,  1.0f, -1.0f },
			{  1.0f, -1.0f, -1.0f },
			{ -1.0f, -1.0f, -1.0f },
			{ -1.0f,  1.0f,  1.0f },
			{  1.0f,  1.0f,  1.0f },
			{  1.0f, -1.0f,  1.0f },
			{ -1.0f, -1.0f,  1.0f },
		};

		// Recreate inverse projectionview matrix with shadow zFar instead
		mat4 invCam;
		glm_perspective(fov, aspect, zNear, zFarShadow, invCam);
		invCam[1][1] *= -1.0f;
		glm_mat4_mul(invCam, gpuCameraData.view, invCam);
		glm_mat4_inv(invCam, invCam);

		// Project frustum corners into world space
		for (uint32_t i = 0; i < 8; i++)
		{
			vec4 frustumCornerV4 = {
				frustumCorners[i][0],
				frustumCorners[i][1],
				frustumCorners[i][2],
				1.0f,
			};
			vec4 invCorner;
			glm_mat4_mulv(invCam, frustumCornerV4, invCorner);
			float_t w = invCorner[3];
			glm_vec4_scale(invCorner, 1.0f / w, invCorner);
			glm_vec4_copy3(invCorner, frustumCorners[i]);
		}

		for (uint32_t i = 0; i < 4; i++)
		{
			vec3 dist;
			glm_vec3_sub(frustumCorners[i + 4], frustumCorners[i], dist);
			vec3 distSplitDist;
			glm_vec3_scale(dist, splitDist, distSplitDist);
			glm_vec3_add(frustumCorners[i], distSplitDist, frustumCorners[i + 4]);
			vec3 distLastSplitDist;
			glm_vec3_scale(dist, lastSplitDist, distLastSplitDist);
			glm_vec3_add(frustumCorners[i], distLastSplitDist, frustumCorners[i]);
		}

		// Get frustum center
		vec3 frustumCenter = GLM_VEC3_ZERO_INIT;
		for (uint32_t i = 0; i < 8; i++)
			glm_vec3_add(frustumCenter, frustumCorners[i], frustumCenter);
		glm_vec3_scale(frustumCenter, 1.0f / 8.0f, frustumCenter);

		float_t radius = 0.0f;
		for (uint32_t i = 0; i < 8; i++)
		{
			float_t distance = glm_vec3_distance(frustumCorners[i], frustumCenter);
			radius = std::max(radius, distance);
		}
		radius = std::ceil(radius * 16.0f) / 16.0f;

		vec3 maxExtents = { radius, radius, radius };
		vec3 minExtents;
		glm_vec3_negate_to(maxExtents, minExtents);

		vec3 lightDir;
		glm_vec3_negate_to(pbrShadingProps.lightDir, lightDir);  // @NOTE: lightDir is direction from surface point to the direction of the light (optimized for shader), but we want the view direction of the light, which is the opposite

		vec3 eye;
		glm_vec3_scale(lightDir, -minExtents[2], eye);
		glm_vec3_sub(frustumCenter, eye, eye);
		mat4 lightViewMatrix;
		glm_lookat(eye, frustumCenter, up, lightViewMatrix);
		mat4 lightOrthoMatrix;
		glm_ortho(minExtents[0], maxExtents[0], minExtents[1], maxExtents[1], 0.0f, maxExtents[2] - minExtents[2], lightOrthoMatrix);

		// Store split distance and matrix in cascade
		glm_mat4_mul(lightOrthoMatrix, lightViewMatrix, gpuCascadeViewProjsData.cascadeViewProjs[i]);
		glm_mat4_copy(gpuCascadeViewProjsData.cascadeViewProjs[i], pbrShadingProps.cascadeViewProjMats[i]);
		pbrShadingProps.cascadeSplits[i] = (nearClip + splitDist * clipRange) * -1.0f;

		lastSplitDist = cascadeSplits[i];
	}
}

//
// MainCamMode
//
void MainCamMode::setMainCamTargetObject(RenderObject* targetObject)
{
	MainCamMode::targetObject = targetObject;
}

void MainCamMode::setOpponentCamTargetObject(physengine::CapsulePhysicsData* targetObject)
{
	MainCamMode::opponentTargetObject = targetObject;
	MainCamMode::opponentTargetTransition.first = true;
}


//
// Camera
//
Camera::Camera(VulkanEngine* engine) : _engine(engine)
{
	// Setup the initial camera mode with ::ENTER event
	_changeEvents[_cameraMode] = CameraModeChangeEvent::ENTER;
}

void Camera::update(const float_t& deltaTime)
{
    //
	// Update camera modes
	// @TODO: scrunch this into its own function
	//
	if (_flagNextStepChangeCameraMode)
	{
		_flagNextStepChangeCameraMode = false;

		_cameraMode = (_cameraMode + 1) % _numCameraModes;
		_changeEvents[_cameraMode] = CameraModeChangeEvent::ENTER;

		debug::pushDebugMessage({
			.message = "Changed to " + std::string(_cameraMode == 0 ? "game camera" : "free camera") + " mode",
			});
	}
	else if (input::onKeyF10Press)  // @NOTE: we never want a state where _flagNextStepChangeCameraMode==true and onKeyF10Press==true are processed. Only one at a time so that there is a dedicated frame for ::ENTER and ::EXIT
	{
		_changeEvents[_cameraMode] = CameraModeChangeEvent::EXIT;
		_flagNextStepChangeCameraMode = true;
	}

	updateMainCam(deltaTime, _changeEvents[_cameraMode_mainCamMode]);
	updateFreeCam(deltaTime, _changeEvents[_cameraMode_freeCamMode]);
	
	// Reset all camera mode change events
	for (size_t i = 0; i < _numCameraModes; i++)
		_changeEvents[i] = CameraModeChangeEvent::NONE;
}

// @NOTE: https://github.com/Unity-Technologies/UnityCsReference/blob/0aa4923aa67e701940c22821c137c8d0184159b2/Runtime/Export/Math/Vector2.cs#L289
inline void smoothDampVec2(vec2& inoutCurrent, vec2 target, vec2& inoutCurrentVelocity, float_t smoothTime, float_t maxSpeed, const float_t& deltaTime)
{
	// Based on Game Programming Gems 4 Chapter 1.10
	smoothTime = std::max(0.000001f, smoothTime);
	float_t omega = 2.0f / smoothTime;

	float_t x = omega * deltaTime;
	float_t exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

	float_t change_x = inoutCurrent[0] - target[0];
	float_t change_y = inoutCurrent[1] - target[1];

	vec2 originalTo;
	glm_vec2_copy(target, originalTo);

	// Clamp maximum speed
	float_t maxChange = maxSpeed * smoothTime;

	float_t sqDist = change_x * change_x + change_y * change_y;  // @TODO: use `glm_vec2_norm2()`
	if (sqDist > maxChange * maxChange)
	{
		float_t mag = std::sqrtf(sqDist);
		change_x = change_x / mag * maxChange;
		change_y = change_y / mag * maxChange;
	}

	target[0] = inoutCurrent[0] - change_x;
	target[1] = inoutCurrent[1] - change_y;

	float_t temp_x = (inoutCurrentVelocity[0] + omega * change_x) * deltaTime;
	float_t temp_y = (inoutCurrentVelocity[1] + omega * change_y) * deltaTime;

	inoutCurrentVelocity[0] = (inoutCurrentVelocity[0] - omega * temp_x) * exp;
	inoutCurrentVelocity[1] = (inoutCurrentVelocity[1] - omega * temp_y) * exp;

	float_t output_x = target[0] + (change_x + temp_x) * exp;
	float_t output_y = target[1] + (change_y + temp_y) * exp;

	// Prevent overshooting
	float_t origMinusCurrent_x = originalTo[0] - inoutCurrent[0];
	float_t origMinusCurrent_y = originalTo[1] - inoutCurrent[1];
	float_t outMinusOrig_x = output_x - originalTo[0];
	float_t outMinusOrig_y = output_y - originalTo[1];

	if (origMinusCurrent_x * outMinusOrig_x + origMinusCurrent_y * outMinusOrig_y > 0)
	{
		output_x = originalTo[0];
		output_y = originalTo[1];

		inoutCurrentVelocity[0] = (output_x - originalTo[0]) / deltaTime;
		inoutCurrentVelocity[1] = (output_y - originalTo[1]) / deltaTime;
	}

	inoutCurrent[0] = output_x;
	inoutCurrent[1] = output_y;
}

// @NOTE: https://github.com/Unity-Technologies/UnityCsReference/blob/0aa4923aa67e701940c22821c137c8d0184159b2/Runtime/Export/Math/Mathf.cs#L309
inline float_t smoothDamp(float_t current, float_t target, float_t& inoutCurrentVelocity, float_t smoothTime, float_t maxSpeed, const float_t& deltaTime)
{
	// Based on Game Programming Gems 4 Chapter 1.10
	smoothTime = std::max(0.000001f, smoothTime);
	float_t omega = 2.0f / smoothTime;

	float_t x = omega * deltaTime;
	float_t exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
	float_t change = current - target;
	float_t originalTo = target;

	// Clamp maximum speed
	float_t maxChange = maxSpeed * smoothTime;
	change = glm_clamp(change, -maxChange, maxChange);
	target = current - change;

	float_t temp = (inoutCurrentVelocity + omega * change) * deltaTime;
	inoutCurrentVelocity = (inoutCurrentVelocity - omega * temp) * exp;
	float_t output = target + (change + temp) * exp;

	// Prevent overshooting
	if (originalTo - current > 0.0f == output > originalTo)
	{
		output = originalTo;
		inoutCurrentVelocity = (output - originalTo) / deltaTime;
	}

	return output;
}

inline float_t smoothDampAngle(float_t current, float_t target, float_t& inoutCurrentVelocity, float_t smoothTime, float_t maxSpeed, const float_t& deltaTime)
{
	// Get closest delta angle within the same 360deg to the target.
	float_t normalizedDeltaAngle = target - current;
	while (normalizedDeltaAngle >= glm_rad(180.0f))  // "Normalize" I guess... delta angle.
		normalizedDeltaAngle -= glm_rad(360.0f);
	while (normalizedDeltaAngle < glm_rad(-180.0f))
		normalizedDeltaAngle += glm_rad(360.0f);
	target = current + normalizedDeltaAngle;

	return smoothDamp(current, target, inoutCurrentVelocity, smoothTime, maxSpeed, deltaTime);
}

void clampXOrbitAngle(float_t& outXOrbitAngle)
{
	const static float_t ANGLE_LIMIT = glm_rad(85.0f);
	outXOrbitAngle = glm_clamp(outXOrbitAngle, -ANGLE_LIMIT, ANGLE_LIMIT);
}

void Camera::updateMainCam(const float_t& deltaTime, CameraModeChangeEvent changeEvent)
{
	bool allowInput = true;
	if (changeEvent != CameraModeChangeEvent::NONE)
	{
		// Calculate orbit angles from the delta angle to focus position
		vec3 fd;
		glm_vec3_copy(sceneCamera.facingDirection, fd);
		vec2 fd_xz = { fd[0], fd[2] };
		mainCamMode.orbitAngles[0] = -atan2f(fd[1], glm_vec2_norm(fd_xz));
		mainCamMode.orbitAngles[1] = atan2f(fd[0], fd[2]);

		SDL_SetRelativeMouseMode(changeEvent == CameraModeChangeEvent::ENTER ? SDL_TRUE : SDL_FALSE);

		if (changeEvent == CameraModeChangeEvent::EXIT)  // Not doing a warp on enter prevents the orbit camera from moving the delta to snap the cursor to the center of the screen which is disorienting
			SDL_WarpMouseInWindow(_engine->_window, _engine->_windowExtent.width / 2, _engine->_windowExtent.height / 2);

		if (changeEvent == CameraModeChangeEvent::ENTER)
		{
			glm_vec2_zero(mainCamMode.focusPositionVelocityXZ);
			mainCamMode.focusPositionVelocityY = 0.0f;
			mainCamMode.actualLookDistanceVelocity = 0.0f;

			glm_vec3_sub(sceneCamera.gpuCameraData.cameraPosition, mainCamMode.focusPositionOffset, mainCamMode.focusPosition);
			mainCamMode.actualLookDistance = 0.0f;
		}

		allowInput = false;
	}
	if (_cameraMode != _cameraMode_mainCamMode)
		return;

	//
	// Focus onto target object
	//
	float_t targetLookDistance = mainCamMode.lookDistance;
	if (mainCamMode.targetObject != nullptr)
	{
		// Update the focus position
		vec3 targetPosition;
		{
			vec4 pos;
			mat4 rot;
			vec3 sca;
			glm_decompose(mainCamMode.targetObject->transformMatrix, pos, rot, sca);
			glm_vec4_copy3(pos, targetPosition);
		}

		if (mainCamMode.opponentTargetObject != nullptr)  // Opponent target position mixing in.
		{
			auto& ott = mainCamMode.opponentTargetTransition;

			// Calculate the total distance for opponent look distance calculation.
			float_t totalDistance = glm_vec3_distance(targetPosition, mainCamMode.opponentTargetObject->interpolBasePosition);
			float_t deltaYPosition = mainCamMode.opponentTargetObject->interpolBasePosition[1] - targetPosition[1];

			// Use dot product between opponent facing direction
			// and current camera direction to find focus position.
			vec2 flatTargetPosition = { targetPosition[0], targetPosition[2] };
			vec2 flatOpponentPosition = { mainCamMode.opponentTargetObject->interpolBasePosition[0], mainCamMode.opponentTargetObject->interpolBasePosition[2] };

			vec2 flatDeltaPosition;
			vec2 normFlatDeltaPosition;
			glm_vec2_sub(flatOpponentPosition, flatTargetPosition, flatDeltaPosition);
			glm_vec2_normalize_to(flatDeltaPosition, normFlatDeltaPosition);

			vec2 normFlatLookDirection = { mainCamMode.calculatedLookDirection[0], mainCamMode.calculatedLookDirection[2] };
			glm_vec2_normalize(normFlatLookDirection);
			
			float_t fDeltaPosDotfLookDir = glm_vec2_dot(normFlatDeltaPosition, normFlatLookDirection);

			float_t midY = glm_lerp(mainCamMode.opponentTargetObject->interpolBasePosition[1], targetPosition[1], 0.5f);
			glm_vec3_lerp(targetPosition, mainCamMode.opponentTargetObject->interpolBasePosition, 1.0f - (fDeltaPosDotfLookDir * 0.5f + 0.5f), targetPosition);
			targetPosition[1] = midY + ott.focusPositionExtraYOffsetWhenTargeting;

			// Initialize state for first time around.
			if (ott.first)
			{
				ott.yOrbitAngleDampVelocity = 0.0f;
				ott.xOrbitAngleDampVelocity = 0.0f;

				// Calculate the target Y orbit angle.
				vec3 lookDirectionRight;
				glm_vec3_crossn(mainCamMode.calculatedLookDirection, vec3{ 0.0f, 1.0f, 0.0f }, lookDirectionRight);

				vec3 normCameraDeltaPosition;
				glm_vec3_sub(mainCamMode.calculatedCameraPosition, targetPosition, normCameraDeltaPosition);
				glm_vec3_normalize(normCameraDeltaPosition);

				float_t side = glm_vec3_dot(lookDirectionRight, normCameraDeltaPosition) > 0.0f ? -1.0f : 1.0f;
				ott.targetYOrbitAngle = atan2f(normFlatDeltaPosition[0], normFlatDeltaPosition[1]) + side * ott.targetYOrbitAngleSideOffset;
			}

			// Update look direction based off previous delta angle.
			float_t newOpponentDeltaAngle = atan2f(normFlatDeltaPosition[0], normFlatDeltaPosition[1]);
			if (!ott.first)
			{
				float_t deltaAngle = newOpponentDeltaAngle - ott.prevOpponentDeltaAngle;
				ott.targetYOrbitAngle += deltaAngle;
				mainCamMode.orbitAngles[1] =
					smoothDampAngle(
						mainCamMode.orbitAngles[1],
						ott.targetYOrbitAngle,
						ott.yOrbitAngleDampVelocity,
						ott.yOrbitAngleSmoothTime,
						std::numeric_limits<float_t>::max(),
						deltaTime
					);
			}
			ott.prevOpponentDeltaAngle = newOpponentDeltaAngle;

			// Update look direction (x axis) from delta position.
			float_t flatDistance = glm_vec2_norm(flatDeltaPosition);
			// ott.targetXOrbitAngle = -atan2f(deltaYPosition, flatDistance) * fDeltaPosDotfLookDir;
			// clampXOrbitAngle(ott.targetXOrbitAngle);
			mainCamMode.orbitAngles[0] =
				smoothDamp(
					mainCamMode.orbitAngles[0],
					ott.targetXOrbitAngle,
					ott.xOrbitAngleDampVelocity,
					ott.xOrbitAngleSmoothTime,
					std::numeric_limits<float_t>::max(),
					deltaTime
				);

			// Calculate the opponent look distance.
			float_t obliqueMultiplier = 1.0f - std::abs(fDeltaPosDotfLookDir);
			ott.calculatedLookDistance =
				ott.lookDistanceBaseAmount +
				ott.lookDistanceObliqueAmount * flatDistance * obliqueMultiplier +
				ott.lookDistanceHeightAmount * std::abs(deltaYPosition);
			targetLookDistance = ott.calculatedLookDistance;

			if (ott.first)
				ott.first = false;
		}

		// Update camera focus position based off the targetPosition.
		if (mainCamMode.focusSmoothTimeXZ > 0.0f)
		{
			vec2 inoutFocusPositionXZ = {
				mainCamMode.focusPosition[0],
				mainCamMode.focusPosition[2],
			};
			vec2 targetPositionXZ = {
				targetPosition[0],
				targetPosition[2],
			};
			smoothDampVec2(
				inoutFocusPositionXZ,
				targetPositionXZ,
				mainCamMode.focusPositionVelocityXZ,
				mainCamMode.focusSmoothTimeXZ,
				std::numeric_limits<float_t>::max(),
				deltaTime
			);
			mainCamMode.focusPosition[0] = inoutFocusPositionXZ[0];
			mainCamMode.focusPosition[2] = inoutFocusPositionXZ[1];
		}
		else
		{
			mainCamMode.focusPosition[0] = targetPosition[0];
			mainCamMode.focusPosition[2] = targetPosition[2];
		}

		if (mainCamMode.focusSmoothTimeY > 0.0f)
		{
			mainCamMode.focusPosition[1] =
				smoothDamp(
					mainCamMode.focusPosition[1],
					targetPosition[1],
					mainCamMode.focusPositionVelocityY,
					mainCamMode.focusSmoothTimeY,
					std::numeric_limits<float_t>::max(),
					deltaTime
				);
		}
		else
			mainCamMode.focusPosition[1] = targetPosition[1];
	}

	//
	// Manual rotation via mouse input
	//
	vec2 mouseDeltaFloatSwizzled = { input::mouseDelta[1], -input::mouseDelta[0] };
	if (allowInput && glm_vec3_norm2(mouseDeltaFloatSwizzled) > 0.000001f)
	{
		vec2 sensitivityRadians = {
			glm_rad(mainCamMode.sensitivity[0]),
			glm_rad(mainCamMode.sensitivity[1]),
		};
		glm_vec2_muladd(mouseDeltaFloatSwizzled, sensitivityRadians, mainCamMode.orbitAngles);

		if (mainCamMode.opponentTargetObject != nullptr)  // Add to target Y orbit angle if opponent. (smol @HACK)
			mainCamMode.opponentTargetTransition.targetYOrbitAngle += mouseDeltaFloatSwizzled[1] * sensitivityRadians[1];
	}

	// Update actual look distance.
	mainCamMode.actualLookDistance =
		smoothDamp(
			mainCamMode.actualLookDistance,
			targetLookDistance,
			mainCamMode.actualLookDistanceVelocity,
			mainCamMode.lookDistanceSmoothTime,
			std::numeric_limits<float_t>::max(),
			deltaTime
		);

	//
	// Recalculate camera
	//
	clampXOrbitAngle(mainCamMode.orbitAngles[0]);
	vec3 lookRotationEuler = {
		mainCamMode.orbitAngles[0],
		mainCamMode.orbitAngles[1],
		0.0f,
	};
	mat4 lookRotation;
	glm_euler_zyx(lookRotationEuler, lookRotation);  // @NOTE: apparently these angles are extrinsic which is what is causing issues
	vec3 forward = { 0.0f, 0.0f, 1.0f };
	glm_mat4_mulv3(lookRotation, forward, 1.0f, mainCamMode.calculatedLookDirection);

	vec3 focusPositionCooked;
	glm_vec3_add(mainCamMode.focusPosition, mainCamMode.focusPositionOffset, focusPositionCooked);

	vec3 calcLookDirectionScaled;
	glm_vec3_scale(mainCamMode.calculatedLookDirection, mainCamMode.actualLookDistance, calcLookDirectionScaled);
	glm_vec3_sub(focusPositionCooked, calcLookDirectionScaled, mainCamMode.calculatedCameraPosition);

	if (glm_vec3_distance2(sceneCamera.facingDirection, mainCamMode.calculatedLookDirection) > 0.0f ||
		glm_vec3_distance2(sceneCamera.gpuCameraData.cameraPosition, mainCamMode.calculatedCameraPosition) > 0.0f)
	{
		glm_vec3_copy(mainCamMode.calculatedLookDirection, sceneCamera.facingDirection);
		glm_vec3_copy(mainCamMode.calculatedCameraPosition, sceneCamera.gpuCameraData.cameraPosition);
		sceneCamera.recalculateSceneCamera(_engine->_pbrRendering.gpuSceneShadingProps);
	}
}

void Camera::updateFreeCam(const float_t& deltaTime, CameraModeChangeEvent changeEvent)
{
	if (changeEvent != CameraModeChangeEvent::NONE)
		freeCamMode.enabled = false;
	if (_cameraMode != _cameraMode_freeCamMode)
		return;

	if (input::onRMBPress || input::onRMBRelease)
	{
		freeCamMode.enabled = (input::RMBPressed && _cameraMode == _cameraMode_freeCamMode);
		SDL_SetRelativeMouseMode(freeCamMode.enabled ? SDL_TRUE : SDL_FALSE);		// @NOTE: this causes cursor to disappear and not leave window boundaries (@BUG: Except for if you right click into the window?)
					
		if (freeCamMode.enabled)
			SDL_GetMouseState(
				&freeCamMode.savedMousePosition[0],
				&freeCamMode.savedMousePosition[1]
			);
		else
			SDL_WarpMouseInWindow(_engine->_window, freeCamMode.savedMousePosition[0], freeCamMode.savedMousePosition[1]);
	}
	
	if (!freeCamMode.enabled)
		return;

	vec2 mousePositionDeltaCooked = {
		input::mouseDelta[0] * freeCamMode.sensitivity,
		input::mouseDelta[1] * freeCamMode.sensitivity,
	};

	vec2 inputToVelocity = GLM_VEC2_ZERO_INIT;
	inputToVelocity[0] += input::keyLeftPressed ? -1.0f : 0.0f;
	inputToVelocity[0] += input::keyRightPressed ? 1.0f : 0.0f;
	inputToVelocity[1] += input::keyUpPressed ? 1.0f : 0.0f;
	inputToVelocity[1] += input::keyDownPressed ? -1.0f : 0.0f;

	float_t worldUpVelocity = 0.0f;
	worldUpVelocity += input::keyWorldUpPressed ? 1.0f : 0.0f;
	worldUpVelocity += input::keyWorldDownPressed ? -1.0f : 0.0f;

	if (glm_vec2_norm(mousePositionDeltaCooked) > 0.0f || glm_vec2_norm(inputToVelocity) > 0.0f || std::abs(worldUpVelocity) > 0.0f)
	{
		vec3 worldUp = { 0.0f, 1.0f, 0.0f };
		vec3 worldDown = { 0.0f, -1.0f, 0.0f };

		// Update camera facing direction with mouse input
		vec3 facingDirectionRight;
		glm_cross(sceneCamera.facingDirection, worldUp, facingDirectionRight);
		glm_normalize(facingDirectionRight);
		mat4 rotation = GLM_MAT4_IDENTITY_INIT;
		glm_rotate(rotation, glm_rad(-mousePositionDeltaCooked[1]), facingDirectionRight);
		vec3 newCamFacingDirection;
		glm_mat4_mulv3(rotation, sceneCamera.facingDirection, 0.0f, newCamFacingDirection);

		if (glm_vec3_angle(newCamFacingDirection, worldUp) > glm_rad(5.0f) &&
			glm_vec3_angle(newCamFacingDirection, worldDown) > glm_rad(5.0f))
			glm_vec3_copy(newCamFacingDirection, sceneCamera.facingDirection);

		glm_mat4_identity(rotation);
		glm_rotate(rotation, glm_rad(-mousePositionDeltaCooked[0]), worldUp);
		glm_mat4_mulv3(rotation, sceneCamera.facingDirection, 0.0f, sceneCamera.facingDirection);

		// Update camera position with keyboard input
		float_t speedMultiplier = input::keyShiftPressed ? 50.0f : 25.0f;
		glm_vec2_scale(inputToVelocity, speedMultiplier * deltaTime, inputToVelocity);
		worldUpVelocity *= speedMultiplier * deltaTime;

		vec3 facingDirectionScaled;
		glm_vec3_scale(sceneCamera.facingDirection, inputToVelocity[1], facingDirectionScaled);
		vec3 facingDirectionRightScaled;
		glm_vec3_scale(facingDirectionRight, inputToVelocity[0], facingDirectionRightScaled);
		vec3 upScaled = {
			0.0f,
			worldUpVelocity,
			0.0f,
		};
		glm_vec3_add(facingDirectionScaled, facingDirectionRightScaled, facingDirectionScaled);
		glm_vec3_addadd(facingDirectionScaled, upScaled, sceneCamera.gpuCameraData.cameraPosition);

		// Recalculate camera
		sceneCamera.recalculateSceneCamera(_engine->_pbrRendering.gpuSceneShadingProps);
	}
}
