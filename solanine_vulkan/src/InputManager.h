#pragma once


namespace input
{
	void processInput(bool* isRunning, bool* isWindowMinimized);

	// Input Types.
	struct OnAction
	{
		bool onAction = false;
		bool _prevState = false;
		inline void update(bool state)
		{
			onAction = (!_prevState && state);
			_prevState = state;
		}
	};

	struct HoldAction
	{
		bool holding = false;
		inline void update(bool state)
		{
			holding = state;
		}
	};

	struct OnHoldReleaseAction
	{
		bool onAction = false;
		bool holding = false;
		bool onRelease = false;
		inline void update(bool state)
		{
			onAction = (!holding && state);
			onRelease = (holding && !state);
			holding = state;
		}
	};

	struct OneAxis
	{
		float_t axis = 0.0f;
		inline void update(float_t state)
		{
			axis = state;
		}
	};

	struct TwoAxis
	{
		float_t axisX = 0.0f;
		float_t axisY = 0.0f;
		inline void update(float_t stateX, float_t stateY)
		{
			axisX = stateX;
			axisY = stateY;
		}
	};

	// Input Sets.
#ifdef _DEVELOP
	struct EditorInputSet
	{
		OnAction            togglePlayEditMode;
		OnAction            playModeToggleSimulation;
		OnAction            playModeCycleCameraModes;
		OnAction            cycleRenderingModes;
		OnAction            toggleEditorUI;
		OnAction            cancel;
		OnAction            submit;
		OnAction            toggleTransformManipulationMode;
		OnAction            switchToTransformPosition;
		OnAction            switchToTransformRotation;
		OnAction            switchToTransformScale;
		OnAction            halveTimescale;
		OnAction            doubleTimescale;
		OnAction            pickObject;
		OnAction            deleteObject;
		OnAction            duplicateObject;
		OnAction            actionC;
		OnAction            actionX;
		OnAction            actionV;
		HoldAction          snapModifier;
		HoldAction          backwardsModifier;

		OneAxis             orbitCamFocusLengthMovement;
		OnHoldReleaseAction orbitCamDrag;
		OnHoldReleaseAction freeCamMode;
		TwoAxis             freeCamMovement;
		OneAxis             verticalFreeCamMovement;
		HoldAction          fastCameraMovement;

		void update();
	};
	void registerEditorInputSetOnThisThread();
	EditorInputSet& editorInputSet();
#endif

	struct RenderThreadInputSet
	{
		OnAction            UIGoLeft;
		OnAction            UIGoRight;
		OnAction            UIGoUp;
		OnAction            UIGoDown;
		OnHoldReleaseAction UIConfirm;
		OnAction            UICancel;
		TwoAxis             UICursorPosition;
		HoldAction          UICursorPositionUpdate;
		TwoAxis             UIScrollDelta;

		TwoAxis             cameraDelta;
		OnAction            toggleInventory;
		OnAction            togglePause;
		OnAction            toggleTransformMenu;

		OnAction            toggleFullscreen;

		void update(float_t deltaTime);
	};
	RenderThreadInputSet& renderInputSet();

	struct SimulationThreadInputSet
	{
		TwoAxis             flatPlaneMovement;
		OnHoldReleaseAction jump;
		OnHoldReleaseAction attack;
		OnHoldReleaseAction detach;
		OnHoldReleaseAction focus;
		OnAction            interact;

		void update();
	};
	SimulationThreadInputSet& simInputSet();
}
