/**
 * AudioAdvanced.h
 * 
 * Advanced Audio Features for Sanic Engine.
 * Inspired by Unreal Engine's audio plugin architecture.
 * 
 * Features:
 * - Convolution Reverb with IR loading
 * - Audio Plugin Interface (FMOD/Wwise compatible)
 * - Ambisonics / Spatial Audio
 * - Audio Occlusion with GPU acceleration
 * - Dynamic Mixing and DSP Effects
 */

#pragma once

#include "AudioSystem.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <complex>
#include <atomic>

namespace Sanic {

// Forward declarations
class VulkanContext;

// ============================================================================
// AUDIO PLUGIN INTERFACE
// ============================================================================

/**
 * Plugin types matching Unreal's audio plugin categories
 */
enum class EAudioPluginType : uint8_t {
    Spatialization = 0,    // 3D audio spatialization (HRTF, binaural)
    Reverb = 1,            // Reverb effects
    Occlusion = 2,         // Sound occlusion/obstruction
    Modulation = 3,        // Parameter modulation
    SourceDataOverride = 4 // Source data processing
};

/**
 * Base interface for all audio plugins
 */
class IAudioPlugin {
public:
    virtual ~IAudioPlugin() = default;
    
    virtual const char* getName() const = 0;
    virtual EAudioPluginType getType() const = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    
    // Plugin settings
    virtual void setParameter(const std::string& name, float value) {}
    virtual float getParameter(const std::string& name) const { return 0.0f; }
};

/**
 * Audio plugin factory interface
 */
class IAudioPluginFactory {
public:
    virtual ~IAudioPluginFactory() = default;
    
    virtual const char* getPluginName() const = 0;
    virtual EAudioPluginType getPluginType() const = 0;
    virtual std::unique_ptr<IAudioPlugin> createPlugin() = 0;
    
    // Discovery
    static void registerFactory(IAudioPluginFactory* factory);
    static const std::vector<IAudioPluginFactory*>& getFactories();
    static IAudioPluginFactory* findFactory(const std::string& name, EAudioPluginType type);
};

// ============================================================================
// SPATIALIZATION PLUGIN INTERFACE
// ============================================================================

struct FSpatializationParams {
    glm::vec3 sourcePosition;
    glm::vec3 sourceVelocity;
    glm::vec3 listenerPosition;
    glm::vec3 listenerForward;
    glm::vec3 listenerUp;
    glm::vec3 listenerVelocity;
    
    float spread = 0.0f;           // 0 = point, 1 = omnidirectional
    float focus = 1.0f;            // Directivity
    float occlusionFactor = 0.0f;  // 0 = unoccluded, 1 = fully occluded
    float distanceAttenuation = 1.0f;
    
    // Environment
    float roomSize = 0.0f;         // For reverb estimation
    float reverbSend = 0.0f;       // Reverb wetness
};

class ISpatializationPlugin : public IAudioPlugin {
public:
    EAudioPluginType getType() const override { return EAudioPluginType::Spatialization; }
    
    /**
     * Process audio with 3D spatialization
     * @param input Mono input buffer
     * @param output Stereo output buffer (interleaved)
     * @param frameCount Number of frames
     * @param params Spatialization parameters
     */
    virtual void spatialize(
        const float* input,
        float* output,
        size_t frameCount,
        const FSpatializationParams& params
    ) = 0;
    
    /**
     * Support for ambisonics output
     */
    virtual bool supportsAmbisonics() const { return false; }
    virtual int getAmbisonicsOrder() const { return 0; }
    virtual void spatializeAmbisonics(
        const float* input,
        float* output,
        size_t frameCount,
        const FSpatializationParams& params
    ) {}
};

// ============================================================================
// REVERB PLUGIN INTERFACE
// ============================================================================

struct FReverbParams {
    float roomSize = 0.5f;
    float damping = 0.5f;
    float wetLevel = 0.3f;
    float dryLevel = 0.7f;
    float width = 1.0f;
    float preDelay = 0.02f;
    float decayTime = 1.5f;
    float density = 1.0f;
    float diffusion = 1.0f;
    float earlyReflections = 0.5f;
    float lateReflections = 0.5f;
    
    // EQ
    float lowCutFreq = 100.0f;
    float highCutFreq = 8000.0f;
    float lowShelfGain = 0.0f;
    float highShelfGain = 0.0f;
};

class IReverbPlugin : public IAudioPlugin {
public:
    EAudioPluginType getType() const override { return EAudioPluginType::Reverb; }
    
    virtual void setParams(const FReverbParams& params) = 0;
    virtual FReverbParams getParams() const = 0;
    
    /**
     * Process stereo audio through reverb
     */
    virtual void process(
        float* buffer,          // In-place stereo buffer
        size_t frameCount,
        uint32_t sampleRate
    ) = 0;
    
    /**
     * Process with separate input/output
     */
    virtual void process(
        const float* input,
        float* output,
        size_t frameCount,
        uint32_t sampleRate
    ) {
        std::memcpy(output, input, frameCount * 2 * sizeof(float));
        process(output, frameCount, sampleRate);
    }
    
    /**
     * Load impulse response for convolution reverb
     */
    virtual bool loadImpulseResponse(const std::string& path) { return false; }
    virtual bool supportsConvolution() const { return false; }
};

// ============================================================================
// OCCLUSION PLUGIN INTERFACE
// ============================================================================

struct FOcclusionParams {
    glm::vec3 sourcePosition;
    glm::vec3 listenerPosition;
    
    // Occlusion result (0-1)
    float directOcclusion = 0.0f;
    float reverbOcclusion = 0.0f;
    
    // For GPU-accelerated occlusion
    VkBuffer sdfBuffer = VK_NULL_HANDLE;
    VkImageView sdfImageView = VK_NULL_HANDLE;
};

class IOcclusionPlugin : public IAudioPlugin {
public:
    EAudioPluginType getType() const override { return EAudioPluginType::Occlusion; }
    
    /**
     * Calculate occlusion between source and listener
     */
    virtual void calculateOcclusion(FOcclusionParams& params) = 0;
    
    /**
     * Batch calculate occlusion for multiple sources
     */
    virtual void calculateOcclusionBatch(
        std::vector<FOcclusionParams>& params
    ) {
        for (auto& p : params) {
            calculateOcclusion(p);
        }
    }
    
    /**
     * GPU-accelerated occlusion
     */
    virtual bool supportsGPU() const { return false; }
    virtual void calculateOcclusionGPU(
        VkCommandBuffer cmd,
        std::vector<FOcclusionParams>& params
    ) {}
};

// ============================================================================
// CONVOLUTION REVERB
// ============================================================================

/**
 * High-quality convolution reverb using FFT
 */
class ConvolutionReverb : public IReverbPlugin {
public:
    ConvolutionReverb();
    ~ConvolutionReverb() override;
    
    const char* getName() const override { return "ConvolutionReverb"; }
    bool initialize() override;
    void shutdown() override;
    
    void setParams(const FReverbParams& params) override;
    FReverbParams getParams() const override { return params_; }
    
    void process(float* buffer, size_t frameCount, uint32_t sampleRate) override;
    
    bool loadImpulseResponse(const std::string& path) override;
    bool supportsConvolution() const override { return true; }
    
    // Additional convolution-specific methods
    bool loadImpulseResponseFromMemory(const float* data, size_t sampleCount, 
                                        uint32_t sampleRate, uint32_t channels);
    void setIRGain(float gain) { irGain_ = gain; }
    float getLatency() const { return latencyMs_; }
    
    // Presets
    static std::vector<std::string> getIRPresets();
    bool loadPreset(const std::string& presetName);
    
private:
    FReverbParams params_;
    float irGain_ = 1.0f;
    float latencyMs_ = 0.0f;
    
    // FFT convolution data
    static constexpr size_t FFT_SIZE = 2048;
    static constexpr size_t HOP_SIZE = FFT_SIZE / 2;
    
    // Impulse response in frequency domain
    std::vector<std::complex<float>> irFreqL_;
    std::vector<std::complex<float>> irFreqR_;
    size_t irLength_ = 0;
    size_t numPartitions_ = 0;
    
    // Overlap-add buffers
    std::vector<float> overlapL_;
    std::vector<float> overlapR_;
    std::vector<float> inputBuffer_;
    size_t inputPos_ = 0;
    
    // FFT buffers
    std::vector<std::complex<float>> fftBuffer_;
    std::vector<float> timeBuffer_;
    
    // FFT implementation (using simple DFT or external library)
    void fft(std::complex<float>* data, size_t n, bool inverse);
    void processPartitioned(const float* input, float* output, size_t frameCount);
};

// ============================================================================
// FMOD/WWISE INTEGRATION INTERFACES
// ============================================================================

/**
 * Abstract interface for middleware audio systems
 */
class IAudioMiddleware {
public:
    virtual ~IAudioMiddleware() = default;
    
    virtual const char* getName() const = 0;
    virtual bool initialize(const std::string& initPath) = 0;
    virtual void shutdown() = 0;
    virtual void update(float deltaTime) = 0;
    
    // Bank/Soundbank management
    virtual bool loadBank(const std::string& bankPath) = 0;
    virtual void unloadBank(const std::string& bankPath) = 0;
    
    // Event playback
    virtual uint64_t playEvent(const std::string& eventPath, const glm::vec3& position = glm::vec3(0)) = 0;
    virtual void stopEvent(uint64_t eventId, bool immediate = false) = 0;
    virtual void setEventParameter(uint64_t eventId, const std::string& paramName, float value) = 0;
    
    // 3D attributes
    virtual void setListenerPosition(const glm::vec3& position, const glm::vec3& forward, 
                                      const glm::vec3& up, const glm::vec3& velocity = glm::vec3(0)) = 0;
    virtual void set3DAttributes(uint64_t eventId, const glm::vec3& position, 
                                  const glm::vec3& velocity = glm::vec3(0)) = 0;
    
    // Global parameters
    virtual void setGlobalParameter(const std::string& paramName, float value) = 0;
    virtual float getGlobalParameter(const std::string& paramName) const = 0;
    
    // Buses/VCAs
    virtual void setBusVolume(const std::string& busPath, float volume) = 0;
    virtual void setBusPaused(const std::string& busPath, bool paused) = 0;
    
    // Memory stats
    virtual size_t getMemoryUsage() const = 0;
    virtual size_t getActiveEventCount() const = 0;
};

/**
 * FMOD Studio integration
 */
class FMODIntegration : public IAudioMiddleware {
public:
    const char* getName() const override { return "FMOD"; }
    
    bool initialize(const std::string& initPath) override;
    void shutdown() override;
    void update(float deltaTime) override;
    
    bool loadBank(const std::string& bankPath) override;
    void unloadBank(const std::string& bankPath) override;
    
    uint64_t playEvent(const std::string& eventPath, const glm::vec3& position) override;
    void stopEvent(uint64_t eventId, bool immediate) override;
    void setEventParameter(uint64_t eventId, const std::string& paramName, float value) override;
    
    void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                             const glm::vec3& up, const glm::vec3& velocity) override;
    void set3DAttributes(uint64_t eventId, const glm::vec3& position,
                         const glm::vec3& velocity) override;
    
    void setGlobalParameter(const std::string& paramName, float value) override;
    float getGlobalParameter(const std::string& paramName) const override;
    
    void setBusVolume(const std::string& busPath, float volume) override;
    void setBusPaused(const std::string& busPath, bool paused) override;
    
    size_t getMemoryUsage() const override;
    size_t getActiveEventCount() const override;
    
    // FMOD-specific features
    void setDopplerScale(float scale);
    void setDistanceFactor(float factor);
    void setRolloffScale(float scale);
    
private:
    // FMOD handles would go here
    // FMOD::Studio::System* studioSystem_ = nullptr;
    // FMOD::System* coreSystem_ = nullptr;
    
    struct EventInstance {
        uint64_t id;
        // FMOD::Studio::EventInstance* instance;
    };
    std::unordered_map<uint64_t, EventInstance> activeEvents_;
    uint64_t nextEventId_ = 1;
    
    bool initialized_ = false;
};

/**
 * Wwise integration
 */
class WwiseIntegration : public IAudioMiddleware {
public:
    const char* getName() const override { return "Wwise"; }
    
    bool initialize(const std::string& initPath) override;
    void shutdown() override;
    void update(float deltaTime) override;
    
    bool loadBank(const std::string& bankPath) override;
    void unloadBank(const std::string& bankPath) override;
    
    uint64_t playEvent(const std::string& eventPath, const glm::vec3& position) override;
    void stopEvent(uint64_t eventId, bool immediate) override;
    void setEventParameter(uint64_t eventId, const std::string& paramName, float value) override;
    
    void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                             const glm::vec3& up, const glm::vec3& velocity) override;
    void set3DAttributes(uint64_t eventId, const glm::vec3& position,
                         const glm::vec3& velocity) override;
    
    void setGlobalParameter(const std::string& paramName, float value) override;
    float getGlobalParameter(const std::string& paramName) const override;
    
    void setBusVolume(const std::string& busPath, float volume) override;
    void setBusPaused(const std::string& busPath, bool paused) override;
    
    size_t getMemoryUsage() const override;
    size_t getActiveEventCount() const override;
    
    // Wwise-specific features
    void setRTPCValue(const std::string& rtpcName, float value, uint64_t gameObjectId = 0);
    void setState(const std::string& stateGroup, const std::string& state);
    void setSwitch(const std::string& switchGroup, const std::string& switchState, uint64_t gameObjectId);
    void postTrigger(const std::string& triggerName, uint64_t gameObjectId = 0);
    
private:
    // AK handles would go here
    // AkGameObjectID listenerId_;
    
    std::unordered_map<uint64_t, uint64_t> eventToGameObject_;
    uint64_t nextGameObjectId_ = 1;
    bool initialized_ = false;
};

// ============================================================================
// GPU AUDIO OCCLUSION
// ============================================================================

/**
 * GPU-accelerated audio occlusion using SDF or ray tracing
 */
class GPUAudioOcclusion : public IOcclusionPlugin {
public:
    GPUAudioOcclusion(VulkanContext& context);
    ~GPUAudioOcclusion() override;
    
    const char* getName() const override { return "GPUAudioOcclusion"; }
    bool initialize() override;
    void shutdown() override;
    
    void calculateOcclusion(FOcclusionParams& params) override;
    void calculateOcclusionBatch(std::vector<FOcclusionParams>& params) override;
    
    bool supportsGPU() const override { return true; }
    void calculateOcclusionGPU(VkCommandBuffer cmd, std::vector<FOcclusionParams>& params) override;
    
    // Configuration
    void setSDF(VkImageView sdfView, VkSampler sdfSampler, const glm::vec3& sdfOrigin, 
                const glm::vec3& sdfSize);
    void setAccelerationStructure(VkAccelerationStructureKHR tlas);
    
    enum class OcclusionMethod {
        SDF,        // March through SDF
        RayTracing, // Hardware ray tracing
        Hybrid      // SDF for far, RT for near
    };
    void setMethod(OcclusionMethod method) { method_ = method; }
    
    // Quality settings
    void setRayCount(uint32_t count) { rayCount_ = count; }
    void setMaxDistance(float distance) { maxDistance_ = distance; }
    
private:
    VulkanContext& context_;
    
    // Compute pipeline for SDF occlusion
    VkPipeline sdfOcclusionPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout sdfPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout sdfDescriptorLayout_ = VK_NULL_HANDLE;
    
    // Ray tracing pipeline for RT occlusion
    VkPipeline rtOcclusionPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout rtPipelineLayout_ = VK_NULL_HANDLE;
    
    // SDF texture
    VkImageView sdfView_ = VK_NULL_HANDLE;
    VkSampler sdfSampler_ = VK_NULL_HANDLE;
    glm::vec3 sdfOrigin_;
    glm::vec3 sdfSize_;
    
    // Acceleration structure
    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    
    OcclusionMethod method_ = OcclusionMethod::SDF;
    uint32_t rayCount_ = 8;
    float maxDistance_ = 100.0f;
    
    // Result buffer
    VkBuffer resultBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory resultMemory_ = VK_NULL_HANDLE;
    
    void createPipelines();
    void marchSDF(const glm::vec3& from, const glm::vec3& to, float& occlusion);
};

// ============================================================================
// AMBISONICS SUPPORT
// ============================================================================

class AmbisonicsEncoder {
public:
    enum class Order { First = 1, Second = 2, Third = 3 };
    
    AmbisonicsEncoder(Order order = Order::First);
    
    /**
     * Encode mono source to ambisonics channels
     * @param input Mono input
     * @param output Ambisonics output (4/9/16 channels for 1st/2nd/3rd order)
     * @param azimuth Horizontal angle in radians
     * @param elevation Vertical angle in radians
     * @param distance Distance for attenuation
     */
    void encode(const float* input, float* output, size_t frameCount,
                float azimuth, float elevation, float distance = 1.0f);
    
    /**
     * Decode ambisonics to binaural stereo
     */
    void decodeBinaural(const float* input, float* output, size_t frameCount);
    
    /**
     * Decode ambisonics to speaker array
     */
    void decodeSpeakers(const float* input, float* output, size_t frameCount,
                        const std::vector<glm::vec2>& speakerPositions);
    
    int getChannelCount() const;
    
private:
    Order order_;
    std::vector<float> shCoefficients_;
    
    // HRTF filters for binaural decode
    std::vector<std::vector<float>> hrtfFilters_;
    
    void computeSHCoefficients(float azimuth, float elevation);
};

// ============================================================================
// DSP EFFECTS CHAIN
// ============================================================================

class IAudioEffect {
public:
    virtual ~IAudioEffect() = default;
    
    virtual const char* getName() const = 0;
    virtual void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) = 0;
    virtual void reset() = 0;
    
    bool bypass = false;
    float mix = 1.0f;  // Dry/wet mix
};

class LowPassFilter : public IAudioEffect {
public:
    const char* getName() const override { return "LowPass"; }
    void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) override;
    void reset() override;
    
    void setCutoff(float freq) { cutoffFreq_ = freq; }
    void setResonance(float q) { resonance_ = q; }
    
private:
    float cutoffFreq_ = 5000.0f;
    float resonance_ = 0.707f;
    float prevSamples_[2][2] = {{0}};
};

class HighPassFilter : public IAudioEffect {
public:
    const char* getName() const override { return "HighPass"; }
    void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) override;
    void reset() override;
    
    void setCutoff(float freq) { cutoffFreq_ = freq; }
    void setResonance(float q) { resonance_ = q; }
    
private:
    float cutoffFreq_ = 100.0f;
    float resonance_ = 0.707f;
    float prevSamples_[2][4] = {{0}};
};

class Compressor : public IAudioEffect {
public:
    const char* getName() const override { return "Compressor"; }
    void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) override;
    void reset() override;
    
    void setThreshold(float db) { thresholdDb_ = db; }
    void setRatio(float ratio) { ratio_ = ratio; }
    void setAttack(float ms) { attackMs_ = ms; }
    void setRelease(float ms) { releaseMs_ = ms; }
    void setMakeupGain(float db) { makeupGainDb_ = db; }
    
private:
    float thresholdDb_ = -20.0f;
    float ratio_ = 4.0f;
    float attackMs_ = 10.0f;
    float releaseMs_ = 100.0f;
    float makeupGainDb_ = 0.0f;
    float envelope_ = 0.0f;
};

class Limiter : public IAudioEffect {
public:
    const char* getName() const override { return "Limiter"; }
    void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) override;
    void reset() override;
    
    void setThreshold(float db) { thresholdDb_ = db; }
    void setRelease(float ms) { releaseMs_ = ms; }
    
private:
    float thresholdDb_ = -1.0f;
    float releaseMs_ = 50.0f;
    float gain_ = 1.0f;
};

class Delay : public IAudioEffect {
public:
    const char* getName() const override { return "Delay"; }
    void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) override;
    void reset() override;
    
    void setDelayTime(float ms) { delayTimeMs_ = ms; }
    void setFeedback(float fb) { feedback_ = fb; }
    
private:
    float delayTimeMs_ = 250.0f;
    float feedback_ = 0.3f;
    std::vector<float> delayBuffer_;
    size_t delayPos_ = 0;
};

class Chorus : public IAudioEffect {
public:
    const char* getName() const override { return "Chorus"; }
    void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) override;
    void reset() override;
    
    void setRate(float hz) { rate_ = hz; }
    void setDepth(float depth) { depth_ = depth; }
    void setMix(float mix) { mix = mix; }
    
private:
    float rate_ = 1.0f;
    float depth_ = 0.5f;
    float phase_ = 0.0f;
    std::vector<float> delayBuffer_;
    size_t writePos_ = 0;
};

class EffectsChain {
public:
    void addEffect(std::unique_ptr<IAudioEffect> effect);
    void removeEffect(size_t index);
    void process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate);
    void reset();
    
    IAudioEffect* getEffect(size_t index);
    size_t getEffectCount() const { return effects_.size(); }
    
private:
    std::vector<std::unique_ptr<IAudioEffect>> effects_;
};

} // namespace Sanic
