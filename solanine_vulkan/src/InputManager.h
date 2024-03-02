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

	struct OnDoubleAction
	{
		bool onDoubleAction = false;
		float_t timer = 0.0f;
		inline void update(bool state, float_t deltaTime)
		{
			onDoubleAction = false;

			if (timer == 0.0f)  // Initial state.
			{
				if (state)
					timer = -1.0f;
			}
			else if (timer == -1.0f)  // Input is pressed.
			{
				if (!state)
					timer = 0.3f;
			}
			else if (timer > 0.0f)  // Input is released. Timer is started to see if input is pressed again before timer is expired.
			{
				if (state)
				{
					onDoubleAction = true;
					timer = -2.0f;
				}
				else
				{
					timer -= deltaTime;
					if (timer <= 0.0f)
						timer = 0.0f;
				}
			}
			else if (timer == -2.0f)  // Double action successfully performed. Now waiting for input to be released so that can revert to initial state.
			{
				if (!state)
					timer = 0.0f;
			}
		}
	};

	struct OnDoubleHoldAction
	{
		bool onDoubleAction = false;
		bool onDoubleHoldAction = false;
		bool onDoubleHoldReleaseAction = false;
		float_t timer = 0.0f;
		inline void update(bool state, float_t deltaTime)
		{
			onDoubleAction = false;
			onDoubleHoldAction = false;
			onDoubleHoldReleaseAction = false;

			if (timer == 0.0f)  // Initial state.
			{
				if (state)
					timer = -1.0f;
			}
			else if (timer == -1.0f)  // Input is pressed.
			{
				if (!state)
					timer = 0.3f;
			}
			else if (timer > 0.0f)  // Input is released. Timer is started to see if input is pressed again before timer is expired.
			{
				if (state)
				{
					timer = -2.0f;
				}
				else
				{
					timer -= deltaTime;
					if (timer <= 0.0f)
						timer = 0.0f;
				}
			}
			else if (timer <= -2.0f)  // Second input is pressed. Release early and `onDoubleAction` is flagged. Release late and `onDoubleHoldReleaseAction` is flagged. Holding does `onDoubleHoldAction`.
			{
				if (state)
				{
					timer -= deltaTime;
					if (timer + deltaTime > -2.3f &&
						timer <= -2.3f)
						onDoubleHoldAction = true;
				}
				else
				{
					if (timer > -2.3f)
						onDoubleAction = true;
					else
						onDoubleHoldReleaseAction = true;
					timer = 0.0f;
				}
			}
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
		OnAction            playModeCycleCameraSubModes;
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
		OneAxis             freeCamOrthoResize;
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
		OnHoldReleaseAction parry;
		OnHoldReleaseAction detach;
		OnHoldReleaseAction focus;
		OnAction            interact;
		OnDoubleHoldAction  respawn;

		void update(float_t deltaTime);
	};
	SimulationThreadInputSet& simInputSet();
}
