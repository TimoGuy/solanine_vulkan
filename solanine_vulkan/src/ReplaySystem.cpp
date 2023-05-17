#include "ReplaySystem.h"


void ReplayData::startRecording(const vec3& startPosition, const float_t& startFacingDirectionRadians)
{
    this->startPosition = startPosition;
    this->startFacingDirectionRadians = startFacingDirectionRadians;
    replayDataSteps.clear();
}

void ReplayData::recordStep(const glm::vec2& worldSpaceInput, const bool& onJumpButton)
{
    if (replayDataSteps.size() &&
        replayDataSteps.back().worldSpaceInput == worldSpaceInput &&
        replayDataSteps.back().onJumpButton == onJumpButton &&
        replayDataSteps.back().numSteps < (unsigned char)255)
    {
        replayDataSteps.back().numSteps++;
        return;
    }

    // New step
    ReplayDataStep step = {
        .worldSpaceInput = worldSpaceInput,
        .onJumpButton = onJumpButton,
        .numSteps = 1,
    };
    replayDataSteps.push_back(step);
}

void ReplayData::playRecording(vec3& outStartPosition, float_t& outStartFacingDirectionRadians)
{
    outStartPosition = startPosition;
    outStartFacingDirectionRadians = startFacingDirectionRadians;

    replayDataStepCurrentIndex = 0;
    replayDataStepInnerIndex = 0;
}

bool ReplayData::playRecordingStep(glm::vec2& outWorldSpaceInput, bool& outOnJumpButton)
{
    // Check if recording is finished
    if (replayDataStepCurrentIndex >= replayDataSteps.size())
        return true;

    // Assign the data to out
    outWorldSpaceInput = replayDataSteps[replayDataStepCurrentIndex].worldSpaceInput;
    outOnJumpButton = replayDataSteps[replayDataStepCurrentIndex].onJumpButton;

    // Increment counter
    replayDataStepInnerIndex++;
    if (replayDataStepInnerIndex < replayDataSteps[replayDataStepCurrentIndex].numSteps)
        return false;
    
    replayDataStepCurrentIndex++;
    replayDataStepInnerIndex = 0;

    // A step was run, thus the recording is not
    // finished (even if this was the last step
    // still return false)
    return false;
}

size_t ReplayData::getRecordingSize()
{
    size_t sizeInBytes = 0;
    sizeInBytes += sizeof(ReplayData::versionNum);
    sizeInBytes += sizeof(ReplayData::startPosition);
    sizeInBytes += sizeof(ReplayData::startFacingDirectionRadians);
    sizeInBytes += sizeof(ReplayData::ReplayDataStep) * replayDataSteps.size();
    return sizeInBytes;
}
