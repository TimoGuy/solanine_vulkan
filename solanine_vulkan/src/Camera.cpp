#include "Camera.h"

#include <string>
#include <SDL2/SDL.h>
#include "ImportGLM.h"
#include "InputManager.h"
#include "RenderObject.h"
#include "PhysicsEngine.h" // physutil::
#include "VulkanEngine.h"  // VulkanEngine
#include "Debug.h"


//
// SceneCamera
//
void SceneCamera::recalculateSceneCamera(GPUPBRShadingProps& pbrShadingProps)
{
	glm::mat4 view = glm::lookAt(gpuCameraData.cameraPosition, gpuCameraData.cameraPosition + facingDirection, { 0.0f, 1.0f, 0.0f });
	glm::mat4 projection = glm::perspective(fov, aspect, zNear, zFar);
	projection[1][1] *= -1.0f;
	gpuCameraData.view = view;
	gpuCameraData.projection = projection;
	gpuCameraData.projectionView = projection * view;

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
	float_t lastSplitDist = 0.0;
	for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
	{
		float_t splitDist = cascadeSplits[i];

		glm::vec3 frustumCorners[8] = {
			glm::vec3(-1.0f,  1.0f, -1.0f),
			glm::vec3( 1.0f,  1.0f, -1.0f),
			glm::vec3( 1.0f, -1.0f, -1.0f),
			glm::vec3(-1.0f, -1.0f, -1.0f),
			glm::vec3(-1.0f,  1.0f,  1.0f),
			glm::vec3( 1.0f,  1.0f,  1.0f),
			glm::vec3( 1.0f, -1.0f,  1.0f),
			glm::vec3(-1.0f, -1.0f,  1.0f),
		};

		// Project frustum corners into world space
		glm::mat4 invCam = glm::inverse(gpuCameraData.projectionView);
		for (uint32_t i = 0; i < 8; i++)
		{
			glm::vec4 invCorner = invCam * glm::vec4(frustumCorners[i], 1.0f);
			frustumCorners[i] = invCorner / invCorner.w;
		}

		for (uint32_t i = 0; i < 4; i++)
		{
			glm::vec3 dist = frustumCorners[i + 4] - frustumCorners[i];
			frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
			frustumCorners[i] = frustumCorners[i] + (dist * lastSplitDist);
		}

		// Get frustum center
		glm::vec3 frustumCenter = glm::vec3(0.0f);
		for (uint32_t i = 0; i < 8; i++)
			frustumCenter += frustumCorners[i];
		frustumCenter /= 8.0f;

		float_t radius = 0.0f;
		for (uint32_t i = 0; i < 8; i++)
		{
			float_t distance = glm::length(frustumCorners[i] - frustumCenter);
			radius = glm::max(radius, distance);
		}
		radius = std::ceil(radius * 16.0f) / 16.0f;

		glm::vec3 maxExtents = glm::vec3(radius);
		glm::vec3 minExtents = -maxExtents;

		const glm::vec3& lightDir = -pbrShadingProps.lightDir;  // @NOTE: lightDir is direction from surface point to the direction of the light (optimized for shader), but we want the view direction of the light, which is the opposite
		glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

		// Store split distance and matrix in cascade
		gpuCascadeViewProjsData.cascadeViewProjs[i] = lightOrthoMatrix * lightViewMatrix;
		pbrShadingProps.cascadeViewProjMats[i] = gpuCascadeViewProjsData.cascadeViewProjs[i];
		pbrShadingProps.cascadeSplits[i] = (nearClip + splitDist * clipRange) * -1.0f;

		lastSplitDist = cascadeSplits[i];
	}

	// Update far plane ratio
	pbrShadingProps.zFarShadowZFarRatio = zFarShadow / zFar;
}

//
// MainCamMode
//
void MainCamMode::setMainCamTargetObject(RenderObject* targetObject)
{
	MainCamMode::targetObject = targetObject;
}


//
// Camera
//
Camera::Camera(VulkanEngine* engine) : _engine(engine)
{}

void Camera::update(const float_t& deltaTime)
{
    //
	// Update camera modes
	// @TODO: scrunch this into its own function
	//
	for (size_t i = 0; i < _numCameraModes; i++)
		_changeEvents[i] = CameraModeChangeEvent::NONE;
	if (_flagNextStepSetEnterChangeEvent && !input::onKeyF10Press)  // If we hit the F10 key two frames in a row, I can see bugs happening without this guard  -Timo
	{
		_flagNextStepSetEnterChangeEvent = false;
		_changeEvents[_cameraMode] = CameraModeChangeEvent::ENTER;
	}
	if (input::onKeyF10Press)
	{
		_prevCameraMode = _cameraMode;
		_cameraMode = fmodf(_cameraMode + 1, _numCameraModes);

		if (!_flagNextStepSetEnterChangeEvent)  // There is a situation where this flag is still true, and that's if the F10 key was hit two frames in a row. We don't need to call EXIT on a camera mode that never ocurred, hence ignoring doing this if the flag is still on  -Timo
		{
			_changeEvents[_prevCameraMode] = CameraModeChangeEvent::EXIT;
			_flagNextStepSetEnterChangeEvent = true;
		}

		debug::pushDebugMessage({
			.message = "Changed to " + std::string(_cameraMode == 0 ? "game camera" : "free camera") + " mode",
			});
	}
	updateMainCam(deltaTime, _changeEvents[_cameraMode_mainCamMode]);
	updateFreeCam(deltaTime, _changeEvents[_cameraMode_freeCamMode]);
}

void Camera::updateMainCam(const float_t& deltaTime, CameraModeChangeEvent changeEvent)
{
	if (changeEvent != CameraModeChangeEvent::NONE)
	{
		mainCamMode.orbitAngles = glm::vec2(glm::radians(45.0f), 0.0f);
		SDL_SetRelativeMouseMode(changeEvent == CameraModeChangeEvent::ENTER ? SDL_TRUE : SDL_FALSE);
		SDL_WarpMouseInWindow(_engine->_window, _engine->_windowExtent.width / 2, _engine->_windowExtent.height / 2);
	}
	if (_cameraMode != _cameraMode_mainCamMode)
		return;

	//
	// Focus onto target object
	//
	if (mainCamMode.targetObject != nullptr)
	{
		// Update the focus position
		glm::vec3 targetPosition = physutil::getPosition(mainCamMode.targetObject->transformMatrix);
		if (mainCamMode.focusRadius > 0.0f)
		{
			glm::vec3 delta = mainCamMode.focusPosition - targetPosition;
			float_t distance = glm::length(delta);
			float_t t = 1.0f;
			if (distance > 0.01f && mainCamMode.focusCentering > 0.0f)
				t = glm::pow(1.0f - mainCamMode.focusCentering, deltaTime);
			if (distance > mainCamMode.focusRadius)
				t = glm::min(t, mainCamMode.focusRadius / distance);
			mainCamMode.focusPosition = targetPosition + delta * t;
		}
		else
			mainCamMode.focusPosition = targetPosition;
	}
	constexpr glm::vec3 worldUp = { 0.0f, 1.0f, 0.0f };

	//
	// Manual rotation via mouse input
	//
	if (glm::length2((glm::vec2)input::mouseDelta) > 0.000001f)
		mainCamMode.orbitAngles += glm::vec2(input::mouseDelta.y, -input::mouseDelta.x) * glm::radians(mainCamMode.sensitivity);

	//
	// Recalculate camera
	//
	mainCamMode.orbitAngles.x = glm::clamp(mainCamMode.orbitAngles.x, glm::radians(-85.0f), glm::radians(85.0f));
	glm::quat lookRotation = glm::quat(glm::vec3(mainCamMode.orbitAngles, 0.0f));
	mainCamMode.calculatedLookDirection = lookRotation * glm::vec3(0, 0, 1);

	const glm::vec3 focusPositionCooked = mainCamMode.focusPosition + mainCamMode.focusPositionOffset;
	float_t lookDistance = mainCamMode.lookDistance;
	auto hitInfo = PhysicsEngine::getInstance().raycast(physutil::toVec3(focusPositionCooked), physutil::toVec3(focusPositionCooked - mainCamMode.calculatedLookDirection * lookDistance));
	if (hitInfo.hasHit())
		lookDistance *= hitInfo.m_closestHitFraction;

	mainCamMode.calculatedCameraPosition = focusPositionCooked - mainCamMode.calculatedLookDirection * lookDistance;

	if (sceneCamera.facingDirection != mainCamMode.calculatedLookDirection ||
		sceneCamera.gpuCameraData.cameraPosition != mainCamMode.calculatedCameraPosition)
	{
		sceneCamera.facingDirection = mainCamMode.calculatedLookDirection;
		sceneCamera.gpuCameraData.cameraPosition = mainCamMode.calculatedCameraPosition;
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
				&freeCamMode.savedMousePosition.x,
				&freeCamMode.savedMousePosition.y
			);
		else
			SDL_WarpMouseInWindow(_engine->_window, freeCamMode.savedMousePosition.x, freeCamMode.savedMousePosition.y);
	}
	
	if (!freeCamMode.enabled)
		return;

	glm::vec2 mousePositionDeltaCooked = (glm::vec2)input::mouseDelta * freeCamMode.sensitivity;

	glm::vec2 inputToVelocity(0.0f);
	inputToVelocity.x += input::keyLeftPressed ? -1.0f : 0.0f;
	inputToVelocity.x += input::keyRightPressed ? 1.0f : 0.0f;
	inputToVelocity.y += input::keyUpPressed ? 1.0f : 0.0f;
	inputToVelocity.y += input::keyDownPressed ? -1.0f : 0.0f;

	float_t worldUpVelocity = 0.0f;
	worldUpVelocity += input::keyWorldUpPressed ? 1.0f : 0.0f;
	worldUpVelocity += input::keyWorldDownPressed ? -1.0f : 0.0f;

	if (glm::length(mousePositionDeltaCooked) > 0.0f || glm::length(inputToVelocity) > 0.0f || glm::abs(worldUpVelocity) > 0.0f)
	{
		constexpr glm::vec3 worldUp = { 0.0f, 1.0f, 0.0f };

		// Update camera facing direction with mouse input
		glm::vec3 newCamFacingDirection =
			glm::rotate(
				sceneCamera.facingDirection,
				glm::radians(-mousePositionDeltaCooked.y),
				glm::normalize(glm::cross(sceneCamera.facingDirection, worldUp))
			);
		if (glm::angle(newCamFacingDirection, worldUp) > glm::radians(5.0f) &&
			glm::angle(newCamFacingDirection, -worldUp) > glm::radians(5.0f))
			sceneCamera.facingDirection = newCamFacingDirection;
		sceneCamera.facingDirection = glm::rotate(sceneCamera.facingDirection, glm::radians(-mousePositionDeltaCooked.x), worldUp);

		// Update camera position with keyboard input
		float speedMultiplier = input::keyShiftPressed ? 50.0f : 25.0f;
		inputToVelocity *= speedMultiplier * deltaTime;
		worldUpVelocity *= speedMultiplier * deltaTime;

		sceneCamera.gpuCameraData.cameraPosition +=
			inputToVelocity.y * sceneCamera.facingDirection +
			inputToVelocity.x * glm::normalize(glm::cross(sceneCamera.facingDirection, worldUp)) +
			glm::vec3(0.0f, worldUpVelocity, 0.0f);

		// Recalculate camera
		sceneCamera.recalculateSceneCamera(_engine->_pbrRendering.gpuSceneShadingProps);
	}
}
