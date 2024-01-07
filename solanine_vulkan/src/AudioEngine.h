// @NOTE: https://www.codyclaborn.me/tutorials/making-a-basic-fmod-audio-engine-in-c/

#pragma once

struct AudioAdapter_FMOD;


class AudioEngine
{
public:
    static AudioEngine& getInstance();

    void initialize();
    void update();
    void cleanup();

    void loadSound(const std::string& fname, bool is3d = true, bool isLooping = false, bool stream = false);
    void unloadSound(const std::string& fname);
    int playSound(const std::string& fname, bool looping = false);
    int playSoundFromList(const std::vector<std::string>& fnames);
    int playSound(const std::string& fname, bool looping, vec3 position, float db = 0.0f);

    void setChannel3dPosition(int channelId, vec3 position);
    void setChannelVolume(int channelId, float db);
    void setChannelLowpassGain(int channelId, float gain);

    void loadBank(const std::string& bankName, FMOD_STUDIO_LOAD_BANK_FLAGS flags);
    void loadEvent(const std::string& eventName);
    void playEvent(const std::string& eventName);
    void stopEvent(const std::string& eventName, bool immediate = false);
    bool isEventPlaying(const std::string& eventName) const;

    void setEventParameter(const std::string& eventName, const std::string& parameterName, float value);
    void getEventParameter(const std::string& eventName, const std::string& parameterName, float* outValue);

    void set3dListenerTransform(vec3 position, vec3 forward);

    void stopChannel(int channelId);
    void stopAllChannels();

    bool isPlaying(int channelId) const;

    inline float dbToVolume(float db) { return powf(10.0f, 0.05f * db); }
    inline float volumeToDb(float volume) { return 20.0f * log10f(volume); }

private:
    AudioEngine() = default;
    ~AudioEngine() {}
    AudioAdapter_FMOD* audioAdapter = nullptr;
};


struct AudioAdapter_FMOD
{
    AudioAdapter_FMOD();
    ~AudioAdapter_FMOD();

    void update();

    FMOD::Studio::System* fmodStudioSystem = nullptr;
    FMOD::System* fmodSystem = nullptr;

    int nextChannelId = 0;

    std::map<std::string, FMOD::Studio::Bank*> banks;
    std::map<std::string, FMOD::Studio::EventInstance*> events;
    std::map<std::string, FMOD::Sound*> sounds;
    std::map<int, FMOD::Channel*> channels;
};
