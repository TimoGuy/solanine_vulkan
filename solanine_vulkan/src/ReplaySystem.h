#pragma once

#include <vector>
#include "ImportGLM.h"


struct ReplayData  // Version 0.  @NOTE: this system relies on the fixed timestep of the physics update. `physicsDeltaTime` cannot change while recording and playing back.
{
    unsigned char versionNum              = 0;
    vec3 startPosition                    = GLM_VEC3_ZERO_INIT;
    float_t   startFacingDirectionRadians = 0.0f;

    struct ReplayDataStep
    {
        vec2          worldSpaceInput;
        bool          onJumpButton;
        unsigned char numSteps;  // Maximum of 255, mind you
    };
    std::vector<ReplayDataStep> replayDataSteps;

    // For playback and recording only (noserialize)
    size_t replayDataStepCurrentIndex;
    size_t replayDataStepInnerIndex;

    void startRecording(vec3 startPosition, const float_t& startFacingDirectionRadians);
    void recordStep(vec2 worldSpaceInput, const bool& onJumpButton);
    void playRecording(vec3& outStartPosition, float_t& outStartFacingDirectionRadians);
    bool playRecordingStep(vec2& outWorldSpaceInput, bool& outOnJumpButton);  // Bool return: whether recording is finished or not (if finished, then will return true and outWorldSpaceInput will not be assigned to)  @NOTE: it will not return true if the last step was just run

    size_t getRecordingSize();  // Returns number of bytes the recording is

    // @TODO: make file export/import versions of this too
};
