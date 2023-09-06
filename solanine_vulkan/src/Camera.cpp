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
	MainCamMode::opponentTargetTransition.active = true;
	MainCamMode::opponentTargetTransition.firstTick = true;
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

		allowInput = false;
	}
	if (_cameraMode != _cameraMode_mainCamMode)
		return;

	//
	// Focus onto target object
	//
	float_t lookDistance = mainCamMode.lookDistance;
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
			// Calculate the total distance for opponent look distance calculation.
			float_t totalDistance = glm_vec3_distance(targetPosition, mainCamMode.opponentTargetObject->interpolBasePosition);

			// Use dot product between opponent facing direction
			// and current camera direction to find focus position.
			vec2 flatTargetPosition = { targetPosition[0], targetPosition[2] };
			vec2 flatOpponentPosition = { mainCamMode.opponentTargetObject->interpolBasePosition[0], mainCamMode.opponentTargetObject->interpolBasePosition[2] };
			vec2 normFlatDeltaPosition;
			glm_vec2_sub(flatOpponentPosition, flatTargetPosition, normFlatDeltaPosition);
			glm_vec2_normalize(normFlatDeltaPosition);
			vec2 normFlatLookDirection = { mainCamMode.calculatedLookDirection[0], mainCamMode.calculatedLookDirection[2] };
			glm_vec2_normalize(normFlatLookDirection);
			
			float_t t = glm_vec2_dot(normFlatDeltaPosition, normFlatLookDirection);
			glm_vec3_lerp(targetPosition, mainCamMode.opponentTargetObject->interpolBasePosition, 1.0f - (t * 0.5f + 0.5f), targetPosition);

			// Update look direction based off previous delta angle.
			float_t newOpponentDeltaAngle = atan2f(normFlatDeltaPosition[0], normFlatDeltaPosition[1]);
			if (!mainCamMode.opponentTargetTransition.active || !mainCamMode.opponentTargetTransition.firstTick)
			{
				float_t deltaAngle = newOpponentDeltaAngle - mainCamMode.opponentTargetTransition.prevOpponentDeltaAngle;
				while (deltaAngle >= glm_rad(180.0f))  // "Normalize" I guess... delta angle.  @COPYPASTA
					deltaAngle -= glm_rad(360.0f);
				while (deltaAngle < glm_rad(-180.0f))
					deltaAngle += glm_rad(360.0f);

				mainCamMode.orbitAngles[1] += deltaAngle;
				if (mainCamMode.opponentTargetTransition.active)
					mainCamMode.opponentTargetTransition.fromYOrbitAngle += deltaAngle;  // @HACK: over the duration of the transition, this is the only way to move the orbit angles.
			}
			mainCamMode.opponentTargetTransition.prevOpponentDeltaAngle = newOpponentDeltaAngle;

			// Process transition (start of targeting opponent)
			auto& ott = mainCamMode.opponentTargetTransition;
			if (ott.active)
			{
				allowInput = false;

				if (ott.firstTick)
				{
					// Start the transition.
					ott.transitionT = 0.0f;
						
					ott.fromYOrbitAngle = mainCamMode.orbitAngles[1];
					ott.fromXOrbitAngle = mainCamMode.orbitAngles[0];

					vec3 lookDirectionRight;
					glm_vec3_crossn(mainCamMode.calculatedLookDirection, vec3{ 0.0f, 1.0f, 0.0f }, lookDirectionRight);

					vec3 normCameraDeltaPosition;
					glm_vec3_sub(mainCamMode.calculatedCameraPosition, targetPosition, normCameraDeltaPosition);
					glm_vec3_normalize(normCameraDeltaPosition);

					float_t side = glm_vec3_dot(lookDirectionRight, normCameraDeltaPosition) > 0.0f ? -1.0f : 1.0f;
					float_t toYOrbitAngle = atan2f(normFlatDeltaPosition[0], normFlatDeltaPosition[1]) + side * ott.targetYOrbitAngleSideOffset;

					ott.deltaYOrbitAngle = toYOrbitAngle - ott.fromYOrbitAngle;
					while (ott.deltaYOrbitAngle >= glm_rad(180.0f))  // "Normalize" I guess... the Y axis orbit delta angle.  @COPYPASTA
						ott.deltaYOrbitAngle -= glm_rad(360.0f);
					while (ott.deltaYOrbitAngle < glm_rad(-180.0f))
						ott.deltaYOrbitAngle += glm_rad(360.0f);

					ott.firstTick = false;
				}
				else
				{
					ott.transitionT += deltaTime * ott.transitionSpeed;
					if (ott.transitionT > 1.0f)
					{
						// Finish the transition.
						ott.transitionT = 1.0f;
						ott.active = false;
					}

					// Update the transition.
					float_t easeT = glm_ease_quad_inout(ott.transitionT);
					mainCamMode.orbitAngles[0] = glm_lerp(ott.fromXOrbitAngle, ott.targetXOrbitAngle, easeT);
					mainCamMode.orbitAngles[1] = ott.fromYOrbitAngle + easeT * ott.deltaYOrbitAngle;
				}
			}

			// Calculate the opponent look distance.
			float_t obliqueMultiplier = 1.0f - std::abs(glm_vec2_dot(normFlatDeltaPosition, normFlatLookDirection));
			ott.calculatedLookDistance = ott.lookDistanceBaseAmount + ott.lookDistanceObliqueAmount * totalDistance * obliqueMultiplier;
			lookDistance = glm_lerp(lookDistance, ott.calculatedLookDistance, glm_ease_quad_inout(ott.transitionT));
		}

		if (mainCamMode.focusRadiusXZ > 0.0f || mainCamMode.focusRadiusY > 0.0f)
		{
			vec3 delta;
			glm_vec3_sub(mainCamMode.focusPosition, targetPosition, delta);

			// XZ axes focusing
			vec2 delta_xz = { delta[0], delta[2] };
			float_t distanceXZ = glm_vec2_norm(delta_xz);
			float_t tXZ = 1.0f;
			if (distanceXZ > 0.01f && mainCamMode.focusCentering > 0.0f)
				tXZ = std::pow(1.0f - mainCamMode.focusCentering, deltaTime);
			if (distanceXZ > mainCamMode.focusRadiusXZ)
				tXZ = std::min(tXZ, mainCamMode.focusRadiusXZ / distanceXZ);

			// Y axis focusing
			float_t distanceY = std::abs(delta[1]);
			float_t tY = 1.0f;
			if (distanceY > 0.01f && mainCamMode.focusCentering > 0.0f)
				tY = std::pow(1.0f - mainCamMode.focusCentering, deltaTime);
			if (distanceY > mainCamMode.focusRadiusY)
				tY = std::min(tY, mainCamMode.focusRadiusY / distanceY);

			vec3 focusingT = { tXZ, tY, tXZ };
			glm_vec3_mul(delta, focusingT, focusingT);
			glm_vec3_add(targetPosition, focusingT, mainCamMode.focusPosition);
		}
		else
			glm_vec3_copy(targetPosition, mainCamMode.focusPosition);
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
	}

	//
	// Recalculate camera
	//
	mainCamMode.orbitAngles[0] = glm_clamp(mainCamMode.orbitAngles[0], glm_rad(-85.0f), glm_rad(85.0f));
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
	glm_vec3_scale(mainCamMode.calculatedLookDirection, lookDistance, calcLookDirectionScaled);
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
