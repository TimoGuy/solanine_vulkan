#pragma once

#include "ImportGLM.h"
#include "Settings.h"
class VulkanEngine;
struct RenderObject;
struct GPUPBRShadingProps;
namespace physengine { struct CapsulePhysicsData; }


struct GPUCameraData
{
	mat4 view;
	mat4 projection;
	mat4 projectionView;
	vec3 cameraPosition = { 5.43231487, 13.2406960, 1.41502118 };
};

struct GPUCascadeViewProjsData
{
	mat4 cascadeViewProjs[SHADOWMAP_CASCADES];
};

//
// Scene Camera
// @NOTE: I think this is a bad name for it. The Scene Camera holds all the data for the GPU,
//        however, the Main cam and the Moving Free cam are both virtual cameras that get switched
//        to back and forth, and just simply change the scene camera's information
//
struct SceneCamera
{
	vec3 facingDirection = { -0.570508420, -0.390730739, 0.722388268 };
	float_t   fov             = glm_rad(70.0f);
	float_t   aspect;
	float_t   zNear           = 1.0f;
	float_t   zFar            = 1000.0f;
	float_t   zFarShadow      = 60.0f;
	vec3 boxCastExtents;
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
	physengine::CapsulePhysicsData* opponentTargetObject = nullptr;
	vec3 focusPosition = GLM_VEC3_ZERO_INIT;
	vec2 focusPositionVelocityXZ;
	float_t focusPositionVelocityY;

	vec2 sensitivity = { 0.1f, 0.1f };
	vec2 orbitAngles = { glm_rad(45.0f), 0.0f };
	vec3 calculatedCameraPosition = GLM_VEC3_ZERO_INIT;
	vec3 calculatedLookDirection = { 0, -0.707106781, 0.707106781 };
	float_t actualLookDistance;
	float_t actualLookDistanceVelocity;

	struct OpponentTargetTransition
	{
		bool first = false;

		float_t targetYOrbitAngleSideOffset = glm_rad(30.0f);
		float_t targetYOrbitAngle;
		float_t yOrbitAngleDampVelocity;

		float_t targetXOrbitAngle = glm_rad(0.0f);
		float_t xOrbitAngleDampVelocity;

		float_t xOrbitAngleSmoothTime = 0.3f;
		float_t yOrbitAngleSmoothTimeSlow = 1.2f;
		float_t yOrbitAngleSmoothTimeFast = 0.5f;
		float_t slowFastTransitionRadius = 3.0f;

		float_t prevOpponentDeltaAngle;  // For turning camera as character orbits opponent.
		float_t calculatedLookDistance;
		float_t lookDistanceBaseAmount = 3.75f;
		float_t lookDistanceObliqueAmount = 0.375f;
		float_t lookDistanceHeightAmount = 1.0f;

		float_t focusPositionExtraYOffsetWhenTargeting = -0.583333f;
	} opponentTargetTransition;

	// Tweak variables
	float_t   lookDistance           = 5.0f;
	float_t   lookDistanceSmoothTime = 0.075f;
	float_t   focusSmoothTimeXZ      = 0.075f;
	float_t   focusSmoothTimeY       = 0.3f;
	vec3      focusPositionOffset    = { 0, 2.333333f, 0 };

	void setMainCamTargetObject(RenderObject* targetObject);
	void setOpponentCamTargetObject(physengine::CapsulePhysicsData* targetObject);
};

#ifdef _DEVELOP
//
// Moving Free cam mode
// (First person camera using wasd and q and e and RMB+mouse to look and move)
//
struct FreeCamMode
{
	bool enabled = false;
	ivec2 savedMousePosition;
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