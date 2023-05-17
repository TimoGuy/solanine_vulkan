#include "AudioEngine.h"

#include <fmod_errors.h>
#include <iostream>
#include "ImportGLM.h"


#define ERRCHECK(_result) errorCheck(_result, __FILE__, __LINE__)
void errorCheck(FMOD_RESULT result, const char* file, int line)
{
    if (result != FMOD_OK)
    {
        std::cerr << "FMOD ERROR:: " << file << " :::: Line " << line << std::endl << "\t" << FMOD_ErrorString(result) << std::endl;
    }
}


AudioEngine& AudioEngine::getInstance()
{
	static AudioEngine instance;
	return instance;
}

void AudioEngine::initialize()
{
    audioAdapter = new AudioAdapter_FMOD();
}

void AudioEngine::update()
{
    audioAdapter->update();
}

void AudioEngine::cleanup()
{
    delete audioAdapter;
}

void AudioEngine::loadSound(const std::string& fname, bool is3d, bool isLooping, bool stream)
{
    if (audioAdapter->sounds.find(fname) != audioAdapter->sounds.end())
        return;     // Sound already loaded up... exit.

    FMOD_MODE mode = FMOD_DEFAULT;
    mode |= is3d ? FMOD_3D : FMOD_2D;
    mode |= isLooping ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;
    mode |= stream ? FMOD_CREATESTREAM : FMOD_CREATECOMPRESSEDSAMPLE;

    FMOD::Sound* sound = nullptr;
    ERRCHECK(audioAdapter->fmodSystem->createSound(fname.c_str(), mode, nullptr, &sound));
    if (sound)
        audioAdapter->sounds[fname] = sound;
}

void AudioEngine::unloadSound(const std::string& fname)
{
    auto sound = audioAdapter->sounds.find(fname);
    if (sound == audioAdapter->sounds.end())
        return;     // Sound doesn't exist in the map... exit.

    ERRCHECK(sound->second->release());
    audioAdapter->sounds.erase(sound);
}

int AudioEngine::playSound(const std::string& fname, bool looping)
{
    return playSound(fname, looping, GLM_VEC3_ZERO_INIT);
}

int AudioEngine::playSoundFromList(const std::vector<std::string>& fnames)
{
    size_t index = rand() % fnames.size();
    return playSound(fnames[index]);
}

int AudioEngine::playSound(const std::string& fname, bool looping, const vec3& position, float db)
{
    int channelId = audioAdapter->nextChannelId++;
    auto sound = audioAdapter->sounds.find(fname);
    if (sound == audioAdapter->sounds.end())
    {
        // Load in the missing sound
        loadSound(fname);
        sound = audioAdapter->sounds.find(fname);
        if (sound == audioAdapter->sounds.end())
        {
            return -1;      // For some reason it failed creating the new sound. Just exit early
        }
    }

    // Setup looping if that's changed
    FMOD_MODE mode;
    sound->second->getMode(&mode);
    if (looping != (bool)(mode & FMOD_LOOP_NORMAL))
    {
        mode &= looping ? ~FMOD_LOOP_OFF : ~FMOD_LOOP_NORMAL;
        mode |= looping ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;
        sound->second->setMode(mode);
    }

    // Play sound paused in case if 3D sound setup is required
    FMOD::Channel* channel = nullptr;
    ERRCHECK(audioAdapter->fmodSystem->playSound(sound->second, nullptr, true, &channel));
    if (channel)
    {
        if (mode & FMOD_3D)
        {
            FMOD_VECTOR fPosition = { position.x, position.y, position.z };
            ERRCHECK(channel->set3DAttributes(&fPosition, nullptr));       // @NOTE: I guess putting in velocity in the future should be useful and good eh!  -Timo
        }
        ERRCHECK(channel->setVolume(dbToVolume(db)));
        ERRCHECK(channel->setPaused(false));
        audioAdapter->channels[channelId] = channel;

        return channelId;
    }
    return -1;      // New channel didn't get created. Show failure.
}

void AudioEngine::setChannel3dPosition(int channelId, const vec3& position)
{
    auto channel = audioAdapter->channels.find(channelId);
    if (channel == audioAdapter->channels.end())
        return;

    FMOD_VECTOR fPosition = { position.x, position.y, position.z };
    ERRCHECK(channel->second->set3DAttributes(&fPosition, nullptr));
}

void AudioEngine::setChannelVolume(int channelId, float db)
{
    auto channel = audioAdapter->channels.find(channelId);
    if (channel == audioAdapter->channels.end())
        return;

    ERRCHECK(channel->second->setVolume(dbToVolume(db)));
}

void AudioEngine::setChannelLowpassGain(int channelId, float gain)
{
    auto channel = audioAdapter->channels.find(channelId);
    if (channel == audioAdapter->channels.end())
        return;

    ERRCHECK(channel->second->setLowPassGain(gain));
}

void AudioEngine::loadBank(const std::string& bankName, FMOD_STUDIO_LOAD_BANK_FLAGS flags)
{
    if (audioAdapter->banks.find(bankName) != audioAdapter->banks.end())
        return;     // Bank was already loaded... exit.

    FMOD::Studio::Bank* bank;
    ERRCHECK(audioAdapter->fmodStudioSystem->loadBankFile(bankName.c_str(), flags, &bank));
    
    if (bank)
    {
        audioAdapter->banks[bankName] = bank;
    }
}

void AudioEngine::loadEvent(const std::string& eventName)
{
    if (audioAdapter->events.find(eventName) != audioAdapter->events.end())
        return;     // Event already loaded... exit.

    FMOD::Studio::EventDescription* eventDescription = nullptr;
    ERRCHECK(audioAdapter->fmodStudioSystem->getEvent(eventName.c_str(), &eventDescription));

    if (eventDescription)
    {
        FMOD::Studio::EventInstance* eventInstance = nullptr;
        ERRCHECK(eventDescription->createInstance(&eventInstance));
        if (eventInstance)
        {
            audioAdapter->events[eventName] = eventInstance;
        }
    }
}

void AudioEngine::playEvent(const std::string& eventName)
{
    auto event = audioAdapter->events.find(eventName);
    if (event == audioAdapter->events.end())
    {
        loadEvent(eventName);
        event = audioAdapter->events.find(eventName);
        if (event == audioAdapter->events.end())
            return;     // Event failed the on-the-fly creation... exit.
    }

    event->second->start();
}

void AudioEngine::stopEvent(const std::string& eventName, bool immediate)
{
    auto event = audioAdapter->events.find(eventName);
    if (event == audioAdapter->events.end())
        return;     // Exit if event not found.

    FMOD_STUDIO_STOP_MODE mode;
    mode = immediate ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT;
    ERRCHECK(event->second->stop(mode));
}

bool AudioEngine::isEventPlaying(const std::string& eventName) const
{
    auto event = audioAdapter->events.find(eventName);
    if (event == audioAdapter->events.end())
        return false;       // Event not found.

    FMOD_STUDIO_PLAYBACK_STATE* state = nullptr;
    return (event->second->getPlaybackState(state) == FMOD_STUDIO_PLAYBACK_PLAYING);
}

void AudioEngine::setEventParameter(const std::string& eventName, const std::string& parameterName, float value)
{
    auto event = audioAdapter->events.find(eventName);
    if (event == audioAdapter->events.end())
        return;     // Exit if event isn't created

    ERRCHECK(event->second->setParameterByName(parameterName.c_str(), value));
}

void AudioEngine::getEventParameter(const std::string& eventName, const std::string& parameterName, float* outValue)
{
    auto event = audioAdapter->events.find(eventName);
    if (event == audioAdapter->events.end())
        return;     // Exit if event isn't created

    ERRCHECK(event->second->getParameterByName(parameterName.c_str(), outValue));
}

void AudioEngine::set3dListenerTransform(const vec3& position, const vec3& forward)
{
    FMOD_3D_ATTRIBUTES attributes = { { 0.0f } };
    attributes.position = { position.x, position.y, position.z };
    attributes.forward = { forward.x, forward.y, forward.z };
    attributes.up.y = 1.0f;
    ERRCHECK(audioAdapter->fmodStudioSystem->setListenerAttributes(0, &attributes));
}

void AudioEngine::stopChannel(int channelId)
{
    auto channel = audioAdapter->channels.find(channelId);
    if (channel == audioAdapter->channels.end())
        return;     // Exit bc channel doesn't exist

    ERRCHECK(channel->second->stop());        // NOTE: let the update() function take care of cleaning up the stopped channels
}

void AudioEngine::stopAllChannels()
{
    for (auto it = audioAdapter->channels.begin(); it != audioAdapter->channels.end(); it++)
    {
        bool isPlaying = false;
        it->second->isPlaying(&isPlaying);
        if (isPlaying)
            ERRCHECK(it->second->stop());
    }
}

bool AudioEngine::isPlaying(int channelId) const
{
    auto channel = audioAdapter->channels.find(channelId);
    if (channel == audioAdapter->channels.end())
        return false;     // Exit bc channel doesn't exist

    bool isPlaying = false;
    channel->second->isPlaying(&isPlaying);
    return isPlaying;
}


//
// FMOD Audio Adapter
//
AudioAdapter_FMOD::AudioAdapter_FMOD()
{
    fmodStudioSystem = nullptr;
    fmodSystem = nullptr;

    ERRCHECK(FMOD::Studio::System::create(&fmodStudioSystem));
    ERRCHECK(fmodStudioSystem->getCoreSystem(&fmodSystem));
    ERRCHECK(fmodSystem->setSoftwareFormat(0, FMOD_SPEAKERMODE_5POINT1, 0));
    ERRCHECK(fmodStudioSystem->initialize(32, FMOD_STUDIO_INIT_LIVEUPDATE, FMOD_INIT_PROFILE_ENABLE, nullptr));        // @NOTE: this may be a problem with the FMOD_STUDIO_INIT_LIVEUPDATE when doing a release build      // @NOTE: Hey, so in the examples the max number of channels was 1024... maybe want to rethink the capacity??  -Timo
}

AudioAdapter_FMOD::~AudioAdapter_FMOD()
{
    ERRCHECK(fmodStudioSystem->unloadAll());
    ERRCHECK(fmodStudioSystem->release());
}

void AudioAdapter_FMOD::update()
{
    std::vector<std::map<int, FMOD::Channel*>::iterator> stoppedChannelRefs;
    for (auto it = channels.begin(); it != channels.end(); it++)
    {
        bool isPlaying = false;
        it->second->isPlaying(&isPlaying);
        if (!isPlaying)
            stoppedChannelRefs.push_back(it);
    }

    for (auto& it : stoppedChannelRefs)
        channels.erase(it);
    
    ERRCHECK(fmodStudioSystem->update());
}
