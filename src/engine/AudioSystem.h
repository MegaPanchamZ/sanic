/**
 * AudioSystem.h
 * 
 * 3D Spatial Audio Engine with Ray Traced Occlusion.
 * 
 * Features:
 * - 3D positional audio with HRTF
 * - Ray traced occlusion using existing RT infrastructure
 * - Reverb zones based on room geometry
 * - Streaming audio for music
 * - Real-time audio mixing
 * 
 * Integration:
 * - Uses miniaudio for cross-platform playback
 * - Queries acceleration structure for occlusion
 * - Uses SDF for fast approximate occlusion
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>

namespace Sanic {

class VulkanContext;

// ============================================================================
// AUDIO CLIP
// ============================================================================

struct AudioClipInfo {
    uint32_t sampleRate = 44100;
    uint32_t channels = 2;
    uint32_t bitsPerSample = 16;
    float duration = 0.0f;
    size_t sampleCount = 0;
    bool streaming = false;
};

class AudioClip {
public:
    AudioClip() = default;
    ~AudioClip();
    
    bool loadFromFile(const std::string& path);
    bool loadFromMemory(const void* data, size_t size, AudioClipInfo info);
    void unload();
    
    const AudioClipInfo& getInfo() const { return info_; }
    const std::vector<float>& getSamples() const { return samples_; }
    
    // For streaming clips
    size_t streamSamples(float* output, size_t frameCount, size_t position);
    
private:
    AudioClipInfo info_;
    std::vector<float> samples_;  // Interleaved samples, normalized to [-1, 1]
    std::string filePath_;
    bool loaded_ = false;
    bool isStreaming_ = false;
    
    // Streaming state
    void* streamHandle_ = nullptr;
};

// ============================================================================
// AUDIO SOURCE (3D sound emitter)
// ============================================================================

struct AudioSourceConfig {
    float volume = 1.0f;
    float pitch = 1.0f;
    float minDistance = 1.0f;      // Distance at which attenuation starts
    float maxDistance = 100.0f;    // Distance at which sound is silent
    float rolloffFactor = 1.0f;    // How quickly sound attenuates
    
    bool loop = false;
    bool is3D = true;
    bool playOnStart = false;
    bool spatialize = true;
    
    // Cone for directional sound
    float coneInnerAngle = 360.0f;  // Full volume
    float coneOuterAngle = 360.0f;  // Zero volume
    float coneOuterVolume = 0.0f;
    
    // Priority (lower = more important, won't be culled)
    int priority = 128;
};

class AudioSource {
public:
    AudioSource();
    ~AudioSource();
    
    void setClip(std::shared_ptr<AudioClip> clip);
    void setConfig(const AudioSourceConfig& config);
    
    void play();
    void pause();
    void stop();
    void setTime(float time);
    
    bool isPlaying() const { return playing_; }
    bool isPaused() const { return paused_; }
    float getTime() const;
    
    // 3D positioning
    void setPosition(const glm::vec3& position);
    void setVelocity(const glm::vec3& velocity);  // For doppler effect
    void setDirection(const glm::vec3& direction);
    
    glm::vec3 getPosition() const { return position_; }
    
    // Volume control
    void setVolume(float volume);
    void setPitch(float pitch);
    
    // Internal - called by AudioSystem
    void updateInternal(float deltaTime, const glm::vec3& listenerPos, 
                       const glm::vec3& listenerForward, float occlusion);
    size_t mixSamples(float* output, size_t frameCount, uint32_t sampleRate);
    
private:
    std::shared_ptr<AudioClip> clip_;
    AudioSourceConfig config_;
    
    glm::vec3 position_ = glm::vec3(0.0f);
    glm::vec3 velocity_ = glm::vec3(0.0f);
    glm::vec3 direction_ = glm::vec3(0.0f, 0.0f, -1.0f);
    
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    size_t samplePosition_ = 0;
    
    float currentVolume_ = 1.0f;
    float currentPitch_ = 1.0f;
    float currentPan_ = 0.0f;  // -1 = left, 1 = right
    float occlusionFactor_ = 0.0f;
    
    // Interpolation for smooth parameter changes
    float targetVolume_ = 1.0f;
    float targetPan_ = 0.0f;
};

// ============================================================================
// AUDIO LISTENER (usually attached to camera)
// ============================================================================

struct AudioListener {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    
    float masterVolume = 1.0f;
};

// ============================================================================
// REVERB ZONE
// ============================================================================

struct ReverbSettings {
    float roomSize = 0.5f;      // 0-1
    float damping = 0.5f;       // 0-1
    float wetMix = 0.3f;        // 0-1
    float dryMix = 0.7f;        // 0-1
    float width = 1.0f;         // 0-1 stereo width
    float earlyReflections = 0.5f;
    float lateReflections = 0.5f;
    float decayTime = 1.5f;     // seconds
    float preDelay = 0.02f;     // seconds
};

class ReverbZone {
public:
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 size = glm::vec3(10.0f);  // Box size
    float blendDistance = 2.0f;          // Fade in/out distance
    
    ReverbSettings settings;
    int priority = 0;  // Higher = more important
    
    bool containsPoint(const glm::vec3& point) const;
    float getBlendWeight(const glm::vec3& point) const;
};

// ============================================================================
// AUDIO SYSTEM
// ============================================================================

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();
    
    bool initialize();
    void shutdown();
    
    // Update (call every frame)
    void update(float deltaTime);
    
    // Listener
    void setListener(const AudioListener& listener);
    const AudioListener& getListener() const { return listener_; }
    
    // Audio clips
    std::shared_ptr<AudioClip> loadClip(const std::string& path);
    void unloadClip(const std::string& path);
    
    // Audio sources
    std::shared_ptr<AudioSource> createSource();
    void destroySource(std::shared_ptr<AudioSource> source);
    
    // Quick play (fire and forget)
    void playOneShot(const std::string& clipPath, const glm::vec3& position, float volume = 1.0f);
    void playOneShot(std::shared_ptr<AudioClip> clip, const glm::vec3& position, float volume = 1.0f);
    
    // Reverb zones
    void addReverbZone(std::shared_ptr<ReverbZone> zone);
    void removeReverbZone(std::shared_ptr<ReverbZone> zone);
    
    // Master controls
    void setMasterVolume(float volume);
    void setMusicVolume(float volume);
    void setSFXVolume(float volume);
    
    float getMasterVolume() const { return masterVolume_; }
    
    // Pause all
    void pauseAll();
    void resumeAll();
    
    // Ray traced occlusion (optional integration)
    void setOcclusionQueryEnabled(bool enabled) { occlusionEnabled_ = enabled; }
    void setSDFTexture(VkImageView sdfView, VkSampler sdfSampler);
    
    // Statistics
    struct Stats {
        uint32_t activeSources;
        uint32_t virtualSources;  // Sources that are playing but not audible
        uint32_t totalClipsLoaded;
        size_t memoryUsedBytes;
        float cpuUsagePercent;
    };
    Stats getStats() const;
    
private:
    // Audio thread callback
    static void audioCallback(void* userData, float* output, size_t frameCount);
    void processAudio(float* output, size_t frameCount);
    
    // Occlusion calculation
    float calculateOcclusion(const glm::vec3& sourcePos);
    
    // Reverb processing
    void applyReverb(float* buffer, size_t frameCount);
    ReverbSettings blendReverbSettings();
    
    AudioListener listener_;
    
    std::vector<std::shared_ptr<AudioSource>> sources_;
    std::vector<std::shared_ptr<ReverbZone>> reverbZones_;
    std::unordered_map<std::string, std::shared_ptr<AudioClip>> clipCache_;
    
    std::mutex sourcesMutex_;
    std::mutex clipsMutex_;
    
    float masterVolume_ = 1.0f;
    float musicVolume_ = 1.0f;
    float sfxVolume_ = 1.0f;
    
    bool initialized_ = false;
    bool occlusionEnabled_ = false;
    
    // miniaudio device handle
    void* device_ = nullptr;
    
    // Mixing buffer
    std::vector<float> mixBuffer_;
    uint32_t sampleRate_ = 44100;
    uint32_t channels_ = 2;
    
    // Reverb state
    std::vector<float> reverbBuffer_;
    std::vector<float> delayLines_[8];  // For early reflections
    size_t delayPositions_[8] = {0};
    
    // SDF occlusion
    VkImageView sdfView_ = VK_NULL_HANDLE;
    VkSampler sdfSampler_ = VK_NULL_HANDLE;
    
    // One-shot pool
    struct OneShotSource {
        std::shared_ptr<AudioSource> source;
        float lifetime;
    };
    std::vector<OneShotSource> oneShotSources_;
};

// ============================================================================
// AUDIO MIXER (for advanced mixing scenarios)
// ============================================================================

class AudioMixerChannel {
public:
    std::string name;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    
    // Effects chain
    bool enableReverb = true;
    bool enableLowPass = false;
    float lowPassCutoff = 5000.0f;
    
    AudioMixerChannel* parent = nullptr;
    std::vector<AudioMixerChannel*> children;
};

class AudioMixer {
public:
    AudioMixer();
    
    AudioMixerChannel* createChannel(const std::string& name, AudioMixerChannel* parent = nullptr);
    AudioMixerChannel* getChannel(const std::string& name);
    
    void setChannelVolume(const std::string& name, float volume);
    void muteChannel(const std::string& name, bool muted);
    void soloChannel(const std::string& name, bool solo);
    
    float getEffectiveVolume(AudioMixerChannel* channel);
    
private:
    std::vector<std::unique_ptr<AudioMixerChannel>> channels_;
    AudioMixerChannel* masterChannel_ = nullptr;
};

// ============================================================================
// AUDIO UTILITIES
// ============================================================================

// Convert between formats
void convertToFloat(const void* input, float* output, size_t sampleCount, 
                   uint32_t bitsPerSample, uint32_t channels);
void convertFromFloat(const float* input, void* output, size_t sampleCount,
                     uint32_t bitsPerSample, uint32_t channels);

// Resampling
void resample(const float* input, size_t inputFrames, uint32_t inputRate,
              float* output, size_t outputFrames, uint32_t outputRate,
              uint32_t channels);

// HRTF (Head-Related Transfer Function) for 3D audio
struct HRTFData {
    std::vector<float> leftIR;   // Impulse response for left ear
    std::vector<float> rightIR;  // Impulse response for right ear
};

class HRTF {
public:
    static HRTF& getInstance();
    
    bool loadDatabase(const std::string& path);
    HRTFData getHRTF(float azimuth, float elevation);  // In radians
    
    void applyHRTF(const float* input, float* output, size_t frameCount,
                   float azimuth, float elevation);
    
private:
    HRTF() = default;
    
    struct HRTFEntry {
        float azimuth;
        float elevation;
        HRTFData data;
    };
    std::vector<HRTFEntry> database_;
};

// Distance attenuation models
float attenuateLinear(float distance, float minDist, float maxDist);
float attenuateInverse(float distance, float minDist, float rolloff);
float attenuateExponential(float distance, float rolloff);

} // namespace Sanic
