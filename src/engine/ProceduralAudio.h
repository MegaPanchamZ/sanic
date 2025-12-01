/**
 * ProceduralAudio.h
 * 
 * Procedural Audio System for Sanic Engine
 * 
 * Features:
 * - Wind synthesis based on player velocity
 * - Dynamic music system with stems and intensity mixing
 * - Speed-based audio effects (Doppler, frequency modulation)
 * - Granular synthesis for environmental sounds
 * - Procedural footstep generation
 * 
 * Reference:
 *   Engine/Source/Runtime/AudioMixer/
 *   Engine/Plugins/Runtime/Metasound/
 */

#pragma once

#include "AudioSystem.h"
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <random>
#include <cmath>

namespace Sanic {

// Forward declarations
class VulkanContext;
class AudioSystem;

// ============================================================================
// WIND SYNTHESIS
// ============================================================================

/**
 * Parameters for wind sound generation
 */
struct WindSynthParams {
    float baseFrequency = 200.0f;        // Base wind frequency (Hz)
    float frequencyRange = 400.0f;       // Frequency variation range
    float amplitudeBase = 0.3f;          // Base amplitude
    float amplitudeVariation = 0.2f;     // Random amplitude variation
    float gustFrequency = 0.5f;          // How often gusts occur (Hz)
    float gustIntensity = 0.4f;          // Gust amplitude multiplier
    float turbulence = 0.3f;             // High-frequency noise amount
    float lowPassCutoff = 2000.0f;       // Low-pass filter cutoff
    float highPassCutoff = 80.0f;        // High-pass filter cutoff
};

/**
 * Wind synthesizer - generates realistic wind sounds based on velocity
 * Uses filtered noise with modulation for natural wind character
 */
class WindSynthesizer {
public:
    WindSynthesizer();
    ~WindSynthesizer();
    
    /**
     * Initialize the synthesizer
     */
    void initialize(uint32_t sampleRate);
    
    /**
     * Shutdown and release resources
     */
    void shutdown();
    
    /**
     * Set wind parameters
     */
    void setParams(const WindSynthParams& params) { params_ = params; }
    const WindSynthParams& getParams() const { return params_; }
    
    /**
     * Update wind state based on player velocity
     * @param velocity Player velocity in world units per second
     * @param deltaTime Frame delta time
     */
    void update(const glm::vec3& velocity, float deltaTime);
    
    /**
     * Set player speed directly (magnitude of velocity)
     */
    void setSpeed(float speed);
    
    /**
     * Get current intensity (0-1)
     */
    float getIntensity() const { return currentIntensity_; }
    
    /**
     * Generate audio samples
     * @param output Stereo output buffer (interleaved)
     * @param frameCount Number of stereo frames to generate
     */
    void synthesize(float* output, size_t frameCount);
    
    /**
     * Speed thresholds for wind intensity mapping
     */
    void setSpeedRange(float minSpeed, float maxSpeed) {
        minSpeed_ = minSpeed;
        maxSpeed_ = maxSpeed;
    }
    
    /**
     * Enable/disable wind layers
     */
    void setLayersEnabled(bool low, bool mid, bool high) {
        lowLayerEnabled_ = low;
        midLayerEnabled_ = mid;
        highLayerEnabled_ = high;
    }
    
private:
    WindSynthParams params_;
    uint32_t sampleRate_ = 48000;
    
    // Speed mapping
    float minSpeed_ = 10.0f;   // Speed where wind starts
    float maxSpeed_ = 700.0f;  // Speed where wind is maximum
    
    // State
    float currentSpeed_ = 0.0f;
    float currentIntensity_ = 0.0f;
    float targetIntensity_ = 0.0f;
    float gustPhase_ = 0.0f;
    float turbulencePhase_ = 0.0f;
    
    // Noise generators
    std::mt19937 rng_;
    std::uniform_real_distribution<float> noiseDist_{-1.0f, 1.0f};
    
    // Filter state (biquad)
    struct BiquadState {
        float x1 = 0, x2 = 0;  // Input history
        float y1 = 0, y2 = 0;  // Output history
        float b0 = 0, b1 = 0, b2 = 0;  // Numerator coefficients
        float a1 = 0, a2 = 0;           // Denominator coefficients
    };
    
    BiquadState lowPassL_, lowPassR_;
    BiquadState highPassL_, highPassR_;
    BiquadState bandPassL_, bandPassR_;
    
    // Layer enables
    bool lowLayerEnabled_ = true;
    bool midLayerEnabled_ = true;
    bool highLayerEnabled_ = true;
    
    // Smoothed values
    float smoothedIntensity_ = 0.0f;
    float smoothedPitch_ = 1.0f;
    
    void updateFilters();
    float applyBiquad(BiquadState& state, float input);
    void designLowPass(BiquadState& state, float cutoff, float q = 0.707f);
    void designHighPass(BiquadState& state, float cutoff, float q = 0.707f);
    void designBandPass(BiquadState& state, float center, float q);
    float generateNoise();
    float generateGust(float time);
};

// ============================================================================
// DYNAMIC MUSIC SYSTEM
// ============================================================================

/**
 * Music stem - individual layer of adaptive music
 */
struct MusicStem {
    std::string name;                    // Stem identifier (e.g., "drums", "bass")
    std::string audioPath;               // Path to audio file
    uint32_t audioSourceId = 0;          // Audio system source ID
    
    float baseVolume = 1.0f;             // Base volume for this stem
    float currentVolume = 0.0f;          // Current interpolated volume
    float targetVolume = 0.0f;           // Target volume
    
    float intensityThreshold = 0.0f;     // Minimum intensity to play (0-1)
    float fadeInTime = 1.0f;             // Time to fade in (seconds)
    float fadeOutTime = 2.0f;            // Time to fade out (seconds)
    
    bool isLooping = true;               // Loop the stem
    bool syncToBar = true;               // Sync start to musical bar
    
    // Beat sync
    int beatsPerBar = 4;
    float currentBeat = 0.0f;
};

/**
 * Game state for music intensity mapping
 */
enum class GameMusicState {
    Idle,           // Standing still, exploring calmly
    Walking,        // Light movement
    Running,        // Fast movement
    HighSpeed,      // 200+ mph
    Boost,          // Boost ability active
    Combat,         // In combat
    Boss,           // Boss fight
    Victory,        // Won combat/level
    Danger,         // Low health/falling
    Cutscene        // Cinematic moment
};

/**
 * Dynamic music configuration
 */
struct DynamicMusicConfig {
    float bpm = 120.0f;                  // Beats per minute
    int beatsPerBar = 4;                 // Time signature
    float crossfadeTime = 2.0f;          // Default crossfade duration
    float intensityLerpSpeed = 0.5f;     // How fast intensity changes
    
    // State to intensity mapping
    std::unordered_map<GameMusicState, float> stateIntensity = {
        { GameMusicState::Idle, 0.1f },
        { GameMusicState::Walking, 0.25f },
        { GameMusicState::Running, 0.5f },
        { GameMusicState::HighSpeed, 0.75f },
        { GameMusicState::Boost, 0.85f },
        { GameMusicState::Combat, 1.0f },
        { GameMusicState::Boss, 1.0f },
        { GameMusicState::Victory, 0.6f },
        { GameMusicState::Danger, 0.9f },
        { GameMusicState::Cutscene, 0.3f }
    };
};

/**
 * Dynamic Music System - adaptive music with intensity-based stem mixing
 */
class DynamicMusicSystem {
public:
    DynamicMusicSystem();
    ~DynamicMusicSystem();
    
    /**
     * Initialize with audio system
     */
    void initialize(AudioSystem* audioSystem);
    
    /**
     * Shutdown and release resources
     */
    void shutdown();
    
    /**
     * Set configuration
     */
    void setConfig(const DynamicMusicConfig& config) { config_ = config; }
    const DynamicMusicConfig& getConfig() const { return config_; }
    
    /**
     * Load a music track with stems
     * @param trackName Name identifier for the track
     * @param stems Vector of stems to load
     */
    void loadTrack(const std::string& trackName, const std::vector<MusicStem>& stems);
    
    /**
     * Unload a track
     */
    void unloadTrack(const std::string& trackName);
    
    /**
     * Play a track
     */
    void playTrack(const std::string& trackName);
    
    /**
     * Stop current track
     * @param fadeOut Whether to fade out
     */
    void stopTrack(bool fadeOut = true);
    
    /**
     * Set game state (affects intensity)
     */
    void setGameState(GameMusicState state);
    
    /**
     * Set intensity directly (0-1)
     */
    void setIntensity(float intensity);
    
    /**
     * Get current intensity
     */
    float getIntensity() const { return currentIntensity_; }
    
    /**
     * Update music system
     * @param deltaTime Frame delta time
     */
    void update(float deltaTime);
    
    /**
     * Get current beat position
     */
    float getCurrentBeat() const { return currentBeat_; }
    
    /**
     * Get current bar position
     */
    int getCurrentBar() const { return currentBar_; }
    
    /**
     * Trigger a stinger (one-shot overlay)
     */
    void playStinger(const std::string& stingerPath);
    
    /**
     * Callbacks for beat sync
     */
    using BeatCallback = std::function<void(int beat, int bar)>;
    void onBeat(BeatCallback callback) { beatCallbacks_.push_back(callback); }
    
    /**
     * Set stem volume override
     */
    void setStemVolume(const std::string& stemName, float volume);
    
    /**
     * Mute/unmute specific stem
     */
    void setStemMuted(const std::string& stemName, bool muted);
    
private:
    AudioSystem* audioSystem_ = nullptr;
    DynamicMusicConfig config_;
    
    // Tracks
    struct MusicTrack {
        std::string name;
        std::vector<MusicStem> stems;
        bool isPlaying = false;
    };
    std::unordered_map<std::string, MusicTrack> tracks_;
    std::string currentTrackName_;
    
    // State
    GameMusicState currentState_ = GameMusicState::Idle;
    float currentIntensity_ = 0.0f;
    float targetIntensity_ = 0.0f;
    
    // Beat tracking
    float currentBeat_ = 0.0f;
    int currentBar_ = 0;
    float beatAccumulator_ = 0.0f;
    int lastBeat_ = -1;
    
    // Callbacks
    std::vector<BeatCallback> beatCallbacks_;
    
    // Stem overrides
    std::unordered_map<std::string, float> stemVolumeOverrides_;
    std::unordered_map<std::string, bool> stemMuteStates_;
    
    void updateStemVolumes(float deltaTime);
    void updateBeatTracking(float deltaTime);
    float getSecondsPerBeat() const;
};

// ============================================================================
// SPEED-BASED AUDIO EFFECTS
// ============================================================================

/**
 * Parameters for speed-based audio processing
 */
struct SpeedAudioParams {
    // Doppler effect
    bool dopplerEnabled = true;
    float dopplerScale = 1.0f;           // Multiplier for doppler effect
    float speedOfSound = 343.0f;         // Speed of sound (m/s)
    
    // Pitch shift based on player speed
    bool speedPitchEnabled = true;
    float minSpeedPitch = 0.95f;         // Pitch at low speed
    float maxSpeedPitch = 1.15f;         // Pitch at max speed
    
    // Low-pass filter based on speed (wind noise occlusion)
    bool windOcclusionEnabled = true;
    float occlusionMinCutoff = 800.0f;   // Cutoff at max speed
    float occlusionMaxCutoff = 20000.0f; // Cutoff at low speed
    
    // Volume ducking for wind
    bool windDuckingEnabled = true;
    float maxWindDuck = 0.3f;            // Max volume reduction (0-1)
    
    // Speed thresholds
    float minSpeed = 0.0f;
    float maxSpeed = 700.0f;
};

/**
 * Speed-based audio effects processor
 * Applies Doppler, pitch shifting, and filtering based on player velocity
 */
class SpeedAudioProcessor {
public:
    SpeedAudioProcessor();
    ~SpeedAudioProcessor();
    
    /**
     * Initialize processor
     */
    void initialize(uint32_t sampleRate);
    
    /**
     * Shutdown
     */
    void shutdown();
    
    /**
     * Set parameters
     */
    void setParams(const SpeedAudioParams& params) { params_ = params; }
    const SpeedAudioParams& getParams() const { return params_; }
    
    /**
     * Update with current player state
     * @param playerPosition Current player position
     * @param playerVelocity Current player velocity
     * @param deltaTime Frame delta time
     */
    void update(const glm::vec3& playerPosition, const glm::vec3& playerVelocity, 
                float deltaTime);
    
    /**
     * Calculate Doppler pitch shift for a sound source
     * @param sourcePosition Source world position
     * @param sourceVelocity Source velocity (can be zero)
     * @return Pitch multiplier (1.0 = no change)
     */
    float calculateDopplerPitch(const glm::vec3& sourcePosition, 
                                 const glm::vec3& sourceVelocity = glm::vec3(0));
    
    /**
     * Get current speed-based pitch modifier
     */
    float getSpeedPitchModifier() const { return currentSpeedPitch_; }
    
    /**
     * Get current wind occlusion cutoff frequency
     */
    float getWindOcclusionCutoff() const { return currentOcclusionCutoff_; }
    
    /**
     * Get current wind ducking amount
     */
    float getWindDuckAmount() const { return currentWindDuck_; }
    
    /**
     * Process audio buffer with speed effects
     * @param buffer Audio buffer (stereo interleaved)
     * @param frameCount Number of frames
     */
    void process(float* buffer, size_t frameCount);
    
    /**
     * Get normalized speed (0-1)
     */
    float getNormalizedSpeed() const { return normalizedSpeed_; }
    
private:
    SpeedAudioParams params_;
    uint32_t sampleRate_ = 48000;
    
    // Player state
    glm::vec3 playerPosition_ = glm::vec3(0);
    glm::vec3 playerVelocity_ = glm::vec3(0);
    float playerSpeed_ = 0.0f;
    float normalizedSpeed_ = 0.0f;
    
    // Calculated values
    float currentSpeedPitch_ = 1.0f;
    float currentOcclusionCutoff_ = 20000.0f;
    float currentWindDuck_ = 0.0f;
    
    // Smoothed values for interpolation
    float smoothedSpeed_ = 0.0f;
    float smoothedPitch_ = 1.0f;
    float smoothedCutoff_ = 20000.0f;
    
    // Low-pass filter for wind occlusion
    struct FilterState {
        float y1 = 0, y2 = 0;
        float x1 = 0, x2 = 0;
    };
    FilterState occlusionFilterL_, occlusionFilterR_;
    
    void updateFilter(float cutoff);
    float applyFilter(FilterState& state, float input, float cutoff);
};

// ============================================================================
// GRANULAR SYNTHESIS
// ============================================================================

/**
 * Grain parameters for granular synthesis
 */
struct GrainParams {
    float position = 0.0f;               // Position in source (0-1)
    float positionVariation = 0.1f;      // Random position offset
    float duration = 0.05f;              // Grain duration (seconds)
    float durationVariation = 0.02f;     // Random duration offset
    float pitch = 1.0f;                  // Pitch multiplier
    float pitchVariation = 0.1f;         // Random pitch offset
    float pan = 0.0f;                    // Stereo pan (-1 to 1)
    float panVariation = 0.2f;           // Random pan offset
    float amplitude = 1.0f;              // Grain amplitude
    float density = 20.0f;               // Grains per second
};

/**
 * Single grain instance
 */
struct Grain {
    float position;          // Current position in source
    float duration;          // Total duration
    float elapsed;           // Time elapsed
    float pitch;             // Pitch multiplier
    float panL, panR;        // Stereo pan gains
    float amplitude;         // Amplitude
    bool active = false;
};

/**
 * Granular synthesizer for environmental sounds
 */
class GranularSynthesizer {
public:
    GranularSynthesizer();
    ~GranularSynthesizer();
    
    /**
     * Initialize with source audio
     */
    void initialize(uint32_t sampleRate);
    
    /**
     * Load source audio
     */
    bool loadSource(const std::string& path);
    
    /**
     * Load source from buffer
     */
    void setSourceBuffer(const std::vector<float>& buffer, uint32_t sourceSampleRate);
    
    /**
     * Set grain parameters
     */
    void setParams(const GrainParams& params) { params_ = params; }
    const GrainParams& getParams() const { return params_; }
    
    /**
     * Start/stop synthesis
     */
    void start();
    void stop();
    bool isPlaying() const { return isPlaying_; }
    
    /**
     * Generate audio output
     */
    void synthesize(float* output, size_t frameCount);
    
    /**
     * Set position in source (0-1)
     */
    void setPosition(float position) { params_.position = position; }
    
    /**
     * Set density (grains per second)
     */
    void setDensity(float density) { params_.density = density; }
    
private:
    GrainParams params_;
    uint32_t sampleRate_ = 48000;
    
    // Source audio
    std::vector<float> sourceBuffer_;
    uint32_t sourceSampleRate_ = 48000;
    
    // Grains
    static constexpr size_t MAX_GRAINS = 64;
    std::array<Grain, MAX_GRAINS> grains_;
    
    // State
    bool isPlaying_ = false;
    float grainTimer_ = 0.0f;
    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_{0.0f, 1.0f};
    
    void spawnGrain();
    float getSourceSample(float position);
    float windowFunction(float t);  // Grain envelope (Hann window)
};

// ============================================================================
// PROCEDURAL FOOTSTEPS
// ============================================================================

/**
 * Surface type for footstep sounds
 */
enum class SurfaceType {
    Concrete,
    Grass,
    Dirt,
    Metal,
    Wood,
    Water,
    Sand,
    Gravel,
    Snow,
    Tile
};

/**
 * Footstep synthesizer parameters
 */
struct FootstepParams {
    SurfaceType surface = SurfaceType::Concrete;
    float speed = 1.0f;                  // Movement speed (affects intensity)
    float weight = 1.0f;                 // Character weight (affects bass)
    float wetness = 0.0f;                // Surface wetness (0-1)
    float intensity = 1.0f;              // Overall intensity
};

/**
 * Procedural footstep generator
 */
class FootstepSynthesizer {
public:
    FootstepSynthesizer();
    ~FootstepSynthesizer();
    
    void initialize(uint32_t sampleRate);
    void shutdown();
    
    /**
     * Trigger a footstep
     */
    void triggerFootstep(const FootstepParams& params);
    
    /**
     * Generate audio
     */
    void synthesize(float* output, size_t frameCount);
    
private:
    uint32_t sampleRate_ = 48000;
    
    // Active footstep sounds
    struct FootstepInstance {
        FootstepParams params;
        float elapsed = 0.0f;
        float duration = 0.0f;
        float noisePhase = 0.0f;
        bool active = false;
    };
    std::vector<FootstepInstance> activeFootsteps_;
    
    std::mt19937 rng_;
    std::uniform_real_distribution<float> noiseDist_{-1.0f, 1.0f};
    
    float synthesizeFootstep(FootstepInstance& step, float time);
    float getSurfaceNoise(SurfaceType surface, float& phase);
    float getSurfaceEnvelope(SurfaceType surface, float t, float duration);
};

// ============================================================================
// PROCEDURAL AUDIO MANAGER
// ============================================================================

/**
 * Main manager for all procedural audio systems
 */
class ProceduralAudioManager {
public:
    static ProceduralAudioManager& getInstance();
    
    /**
     * Initialize all procedural audio systems
     */
    void initialize(AudioSystem* audioSystem, uint32_t sampleRate);
    
    /**
     * Shutdown all systems
     */
    void shutdown();
    
    /**
     * Update all systems
     */
    void update(float deltaTime);
    
    /**
     * Update player state (position, velocity)
     */
    void updatePlayerState(const glm::vec3& position, const glm::vec3& velocity);
    
    /**
     * Set game state for dynamic music
     */
    void setGameState(GameMusicState state);
    
    /**
     * Trigger footstep
     */
    void triggerFootstep(SurfaceType surface, float intensity = 1.0f);
    
    /**
     * Get individual systems
     */
    WindSynthesizer& getWindSynth() { return windSynth_; }
    DynamicMusicSystem& getDynamicMusic() { return dynamicMusic_; }
    SpeedAudioProcessor& getSpeedProcessor() { return speedProcessor_; }
    GranularSynthesizer& getGranularSynth() { return granularSynth_; }
    FootstepSynthesizer& getFootstepSynth() { return footstepSynth_; }
    
    /**
     * Generate mixed procedural audio
     * @param output Stereo interleaved output
     * @param frameCount Number of frames
     */
    void synthesize(float* output, size_t frameCount);
    
    /**
     * Enable/disable individual systems
     */
    void setWindEnabled(bool enabled) { windEnabled_ = enabled; }
    void setMusicEnabled(bool enabled) { musicEnabled_ = enabled; }
    void setSpeedEffectsEnabled(bool enabled) { speedEffectsEnabled_ = enabled; }
    
private:
    ProceduralAudioManager() = default;
    
    AudioSystem* audioSystem_ = nullptr;
    uint32_t sampleRate_ = 48000;
    
    // Subsystems
    WindSynthesizer windSynth_;
    DynamicMusicSystem dynamicMusic_;
    SpeedAudioProcessor speedProcessor_;
    GranularSynthesizer granularSynth_;
    FootstepSynthesizer footstepSynth_;
    
    // Player state
    glm::vec3 playerPosition_ = glm::vec3(0);
    glm::vec3 playerVelocity_ = glm::vec3(0);
    
    // Enables
    bool windEnabled_ = true;
    bool musicEnabled_ = true;
    bool speedEffectsEnabled_ = true;
    
    // Mixing buffers
    std::vector<float> windBuffer_;
    std::vector<float> granularBuffer_;
    std::vector<float> footstepBuffer_;
};

} // namespace Sanic
