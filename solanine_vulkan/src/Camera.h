#pragma once

#include "ImportGLM.h"
#include "Settings.h"
class VulkanEngine;
struct RenderObject;
struct GPUPBRShadingProps;


struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 projectionView;
	glm::vec3 cameraPosition;
};

struct GPUCascadeViewProjsData
{
	glm::mat4 cascadeViewProjs[SHADOWMAP_CASCADES];
};

//
// Scene Camera
// @NOTE: I think this is a bad name for it. The Scene Camera holds all the data for the GPU,
//        however, the Main cam and the Moving Free cam are both virtual cameras that get switched
//        to back and forth, and just simply change the scene camera's information
//
struct SceneCamera
{
	glm::vec3 facingDirection = { 0.0f, 0.0f, 1.0f };
	float_t fov               = glm::radians(70.0f);
	float_t aspect;
	float_t zNear             = 0.1f;
	float_t zFar              = 1500.0f;
	float_t zFarShadow        = 200.0f;
	GPUCameraData gpuCameraData;
	GPUCascadeViewProjsData gpuCascadeViewProjsData;  // This will get calculated from the scene camera since this is a CSM viewprojs

	void recalculateSceneCamera(GPUPBRShadingProps& pbrShadingProps);
	void recalculateCascadeViewProjs(GPUPBRShadingProps& pbrShadingProps);
};

//
// Main cam mode
// (Orbiting camera using just mouse inputs)
//
enum class CameraModeChangeEvent { NONE, ENTER, EXIT };

struct MainCamMode
{
	RenderObject* targetObject = nullptr;
	glm::vec3 focusPosition = glm::vec3(0);
	glm::vec2 sensitivity = glm::vec2(0.1f, 0.1f);
	glm::vec2 orbitAngles = glm::vec2(glm::radians(45.0f), 0.0f);
	glm::vec3 calculatedCameraPosition = glm::vec3(0);
	glm::vec3 calculatedLookDirection = glm::vec3(0, -0.707106781, 0.707106781);

	// Tweak variables
	float_t   lookDistance        = 15.0f;
	float_t   focusRadiusXZ       = 3.0f;
	float_t   focusRadiusY        = 7.0f;
	float_t   focusCentering      = 0.75f;
	glm::vec3 focusPositionOffset = glm::vec3(0, 7, 0);

	void setMainCamTargetObject(RenderObject* targetObject);
};

#ifdef _DEVELOP
//
// Moving Free cam mode
// (First person camera using wasd and q and e and RMB+mouse to look and move)
//
struct FreeCamMode
{
	bool enabled = false;
	glm::ivec2 savedMousePosition;
	float_t sensitivity = 0.1f;
};
#endif

//
// Contains all of the camera information
//
struct Camera
{
	Camera(VulkanEngine* engine);

	SceneCamera    sceneCamera;
	MainCamMode    mainCamMode;
#ifdef _DEVELOP
	FreeCamMode    freeCamMode;
#endif

	const static uint32_t
		_cameraMode_mainCamMode = 0,
		_cameraMode_freeCamMode = 1;
	inline uint32_t getCameraMode() { return _cameraMode; }

	void update(const float_t& deltaTime);

private:
	VulkanEngine* _engine;

	static const uint32_t _numCameraModes = 2;
	uint32_t _cameraMode = _cameraMode_freeCamMode;
	CameraModeChangeEvent _changeEvents[_numCameraModes];
	bool _flagNextStepChangeCameraMode = false;

	void updateMainCam(const float_t& deltaTime, CameraModeChangeEvent changeEvent);
	void updateFreeCam(const float_t& deltaTime, CameraModeChangeEvent changeEvent);
};