/**
 * ProceduralAudio.cpp
 * 
 * Implementation of procedural audio systems
 */

#include "ProceduralAudio.h"
#include "AudioSystem.h"
#include <algorithm>
#include <cstring>

namespace Sanic {

// ============================================================================
// WIND SYNTHESIZER
// ============================================================================

WindSynthesizer::WindSynthesizer() 
    : rng_(std::random_device{}()) {
}

WindSynthesizer::~WindSynthesizer() {
    shutdown();
}

void WindSynthesizer::initialize(uint32_t sampleRate) {
    sampleRate_ = sampleRate;
    updateFilters();
}

void WindSynthesizer::shutdown() {
    // Reset state
    currentSpeed_ = 0.0f;
    currentIntensity_ = 0.0f;
}

void WindSynthesizer::update(const glm::vec3& velocity, float deltaTime) {
    setSpeed(glm::length(velocity));
    
    // Smooth intensity changes
    float lerpFactor = 1.0f - std::exp(-deltaTime * 3.0f);
    currentIntensity_ = glm::mix(currentIntensity_, targetIntensity_, lerpFactor);
    
    // Update gust phase
    gustPhase_ += deltaTime * params_.gustFrequency;
    if (gustPhase_ > 1.0f) gustPhase_ -= 1.0f;
}

void WindSynthesizer::setSpeed(float speed) {
    currentSpeed_ = speed;
    
    // Map speed to intensity
    float normalizedSpeed = glm::clamp((speed - minSpeed_) / (maxSpeed_ - minSpeed_), 0.0f, 1.0f);
    targetIntensity_ = normalizedSpeed;
}

void WindSynthesizer::synthesize(float* output, size_t frameCount) {
    if (currentIntensity_ < 0.001f) {
        std::memset(output, 0, frameCount * 2 * sizeof(float));
        return;
    }
    
    float dt = 1.0f / static_cast<float>(sampleRate_);
    
    for (size_t i = 0; i < frameCount; ++i) {
        // Generate base noise
        float noise = generateNoise();
        
        // Apply turbulence (higher frequency noise)
        turbulencePhase_ += dt * 50.0f;
        float turbulence = std::sin(turbulencePhase_ * 17.3f) * 
                          std::sin(turbulencePhase_ * 31.7f) * params_.turbulence;
        
        // Gust modulation
        float gustMod = generateGust(gustPhase_);
        
        // Combine layers
        float signal = noise * (1.0f + turbulence) * (1.0f + gustMod * params_.gustIntensity);
        
        // Apply intensity
        signal *= currentIntensity_ * params_.amplitudeBase;
        
        // Apply filters for left channel
        float left = signal + noiseDist_(rng_) * 0.1f * currentIntensity_;
        left = applyBiquad(highPassL_, left);
        left = applyBiquad(lowPassL_, left);
        
        // Slightly different for right channel (stereo width)
        float right = signal + noiseDist_(rng_) * 0.1f * currentIntensity_;
        right = applyBiquad(highPassR_, right);
        right = applyBiquad(lowPassR_, right);
        
        output[i * 2 + 0] = left;
        output[i * 2 + 1] = right;
    }
}

float WindSynthesizer::generateNoise() {
    return noiseDist_(rng_);
}

float WindSynthesizer::generateGust(float phase) {
    // Smooth gust envelope using sine
    return (std::sin(phase * 6.28318f) + 1.0f) * 0.5f;
}

void WindSynthesizer::updateFilters() {
    float cutoffNorm = params_.lowPassCutoff / static_cast<float>(sampleRate_);
    designLowPass(lowPassL_, cutoffNorm);
    designLowPass(lowPassR_, cutoffNorm);
    
    cutoffNorm = params_.highPassCutoff / static_cast<float>(sampleRate_);
    designHighPass(highPassL_, cutoffNorm);
    designHighPass(highPassR_, cutoffNorm);
}

float WindSynthesizer::applyBiquad(BiquadState& state, float input) {
    float output = state.b0 * input + state.b1 * state.x1 + state.b2 * state.x2
                 - state.a1 * state.y1 - state.a2 * state.y2;
    
    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = output;
    
    return output;
}

void WindSynthesizer::designLowPass(BiquadState& state, float cutoff, float q) {
    float omega = 2.0f * 3.14159f * cutoff;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * q);
    
    float a0 = 1.0f + alpha;
    state.b0 = ((1.0f - cosOmega) / 2.0f) / a0;
    state.b1 = (1.0f - cosOmega) / a0;
    state.b2 = state.b0;
    state.a1 = (-2.0f * cosOmega) / a0;
    state.a2 = (1.0f - alpha) / a0;
}

void WindSynthesizer::designHighPass(BiquadState& state, float cutoff, float q) {
    float omega = 2.0f * 3.14159f * cutoff;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * q);
    
    float a0 = 1.0f + alpha;
    state.b0 = ((1.0f + cosOmega) / 2.0f) / a0;
    state.b1 = -(1.0f + cosOmega) / a0;
    state.b2 = state.b0;
    state.a1 = (-2.0f * cosOmega) / a0;
    state.a2 = (1.0f - alpha) / a0;
}

void WindSynthesizer::designBandPass(BiquadState& state, float center, float q) {
    float omega = 2.0f * 3.14159f * center;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * q);
    
    float a0 = 1.0f + alpha;
    state.b0 = alpha / a0;
    state.b1 = 0.0f;
    state.b2 = -alpha / a0;
    state.a1 = (-2.0f * cosOmega) / a0;
    state.a2 = (1.0f - alpha) / a0;
}

// ============================================================================
// DYNAMIC MUSIC SYSTEM
// ============================================================================

DynamicMusicSystem::DynamicMusicSystem() = default;

DynamicMusicSystem::~DynamicMusicSystem() {
    shutdown();
}

void DynamicMusicSystem::initialize(AudioSystem* audioSystem) {
    audioSystem_ = audioSystem;
}

void DynamicMusicSystem::shutdown() {
    stopTrack(false);
    tracks_.clear();
}

void DynamicMusicSystem::loadTrack(const std::string& trackName, 
                                    const std::vector<MusicStem>& stems) {
    MusicTrack track;
    track.name = trackName;
    track.stems = stems;
    
    // Load audio sources for each stem
    for (auto& stem : track.stems) {
        // Audio loading would happen here through audioSystem_
        stem.currentVolume = 0.0f;
        stem.targetVolume = 0.0f;
    }
    
    tracks_[trackName] = std::move(track);
}

void DynamicMusicSystem::unloadTrack(const std::string& trackName) {
    if (currentTrackName_ == trackName) {
        stopTrack(false);
    }
    tracks_.erase(trackName);
}

void DynamicMusicSystem::playTrack(const std::string& trackName) {
    auto it = tracks_.find(trackName);
    if (it == tracks_.end()) return;
    
    // Stop current track
    if (!currentTrackName_.empty() && currentTrackName_ != trackName) {
        stopTrack(true);
    }
    
    currentTrackName_ = trackName;
    it->second.isPlaying = true;
    
    // Reset beat tracking
    currentBeat_ = 0.0f;
    currentBar_ = 0;
    beatAccumulator_ = 0.0f;
    lastBeat_ = -1;
    
    // Start playing stems (audio system would handle actual playback)
    for (auto& stem : it->second.stems) {
        stem.currentBeat = 0.0f;
    }
}

void DynamicMusicSystem::stopTrack(bool fadeOut) {
    auto it = tracks_.find(currentTrackName_);
    if (it == tracks_.end()) return;
    
    if (fadeOut) {
        // Set all stems to fade out
        for (auto& stem : it->second.stems) {
            stem.targetVolume = 0.0f;
        }
    } else {
        // Immediate stop
        for (auto& stem : it->second.stems) {
            stem.currentVolume = 0.0f;
            stem.targetVolume = 0.0f;
        }
        it->second.isPlaying = false;
    }
    
    currentTrackName_.clear();
}

void DynamicMusicSystem::setGameState(GameMusicState state) {
    currentState_ = state;
    
    auto it = config_.stateIntensity.find(state);
    if (it != config_.stateIntensity.end()) {
        targetIntensity_ = it->second;
    }
}

void DynamicMusicSystem::setIntensity(float intensity) {
    targetIntensity_ = glm::clamp(intensity, 0.0f, 1.0f);
}

void DynamicMusicSystem::update(float deltaTime) {
    // Smooth intensity transition
    float lerpSpeed = config_.intensityLerpSpeed * deltaTime;
    currentIntensity_ = glm::mix(currentIntensity_, targetIntensity_, lerpSpeed);
    
    // Update beat tracking
    updateBeatTracking(deltaTime);
    
    // Update stem volumes
    updateStemVolumes(deltaTime);
}

void DynamicMusicSystem::updateBeatTracking(float deltaTime) {
    float secondsPerBeat = getSecondsPerBeat();
    
    beatAccumulator_ += deltaTime;
    
    while (beatAccumulator_ >= secondsPerBeat) {
        beatAccumulator_ -= secondsPerBeat;
        currentBeat_ += 1.0f;
        
        int beatInt = static_cast<int>(currentBeat_) % config_.beatsPerBar;
        if (beatInt == 0 && lastBeat_ != 0) {
            currentBar_++;
        }
        lastBeat_ = beatInt;
        
        // Fire beat callbacks
        for (auto& callback : beatCallbacks_) {
            callback(beatInt, currentBar_);
        }
    }
}

void DynamicMusicSystem::updateStemVolumes(float deltaTime) {
    auto it = tracks_.find(currentTrackName_);
    if (it == tracks_.end()) return;
    
    for (auto& stem : it->second.stems) {
        // Determine target volume based on intensity
        float targetVol = 0.0f;
        if (currentIntensity_ >= stem.intensityThreshold) {
            targetVol = stem.baseVolume;
            
            // Check for overrides
            auto overrideIt = stemVolumeOverrides_.find(stem.name);
            if (overrideIt != stemVolumeOverrides_.end()) {
                targetVol *= overrideIt->second;
            }
            
            // Check mute state
            auto muteIt = stemMuteStates_.find(stem.name);
            if (muteIt != stemMuteStates_.end() && muteIt->second) {
                targetVol = 0.0f;
            }
        }
        
        stem.targetVolume = targetVol;
        
        // Interpolate volume
        float fadeTime = (stem.targetVolume > stem.currentVolume) ? 
                         stem.fadeInTime : stem.fadeOutTime;
        float fadeSpeed = deltaTime / std::max(fadeTime, 0.001f);
        
        stem.currentVolume = glm::mix(stem.currentVolume, stem.targetVolume, 
                                       std::min(fadeSpeed, 1.0f));
        
        // Apply to audio source (audio system would handle this)
        // audioSystem_->setSourceVolume(stem.audioSourceId, stem.currentVolume);
    }
}

float DynamicMusicSystem::getSecondsPerBeat() const {
    return 60.0f / config_.bpm;
}

void DynamicMusicSystem::playStinger(const std::string& stingerPath) {
    // One-shot stinger playback through audio system
    // audioSystem_->playOneShot(stingerPath);
}

void DynamicMusicSystem::setStemVolume(const std::string& stemName, float volume) {
    stemVolumeOverrides_[stemName] = glm::clamp(volume, 0.0f, 1.0f);
}

void DynamicMusicSystem::setStemMuted(const std::string& stemName, bool muted) {
    stemMuteStates_[stemName] = muted;
}

// ============================================================================
// SPEED AUDIO PROCESSOR
// ============================================================================

SpeedAudioProcessor::SpeedAudioProcessor() = default;

SpeedAudioProcessor::~SpeedAudioProcessor() {
    shutdown();
}

void SpeedAudioProcessor::initialize(uint32_t sampleRate) {
    sampleRate_ = sampleRate;
}

void SpeedAudioProcessor::shutdown() {
    // Reset state
    playerSpeed_ = 0.0f;
    normalizedSpeed_ = 0.0f;
}

void SpeedAudioProcessor::update(const glm::vec3& playerPosition, 
                                  const glm::vec3& playerVelocity,
                                  float deltaTime) {
    playerPosition_ = playerPosition;
    playerVelocity_ = playerVelocity;
    playerSpeed_ = glm::length(playerVelocity);
    
    // Normalize speed
    normalizedSpeed_ = glm::clamp(
        (playerSpeed_ - params_.minSpeed) / (params_.maxSpeed - params_.minSpeed),
        0.0f, 1.0f
    );
    
    // Smooth transitions
    float smoothFactor = 1.0f - std::exp(-deltaTime * 5.0f);
    smoothedSpeed_ = glm::mix(smoothedSpeed_, normalizedSpeed_, smoothFactor);
    
    // Calculate speed-based pitch
    if (params_.speedPitchEnabled) {
        float targetPitch = glm::mix(params_.minSpeedPitch, params_.maxSpeedPitch, smoothedSpeed_);
        smoothedPitch_ = glm::mix(smoothedPitch_, targetPitch, smoothFactor);
        currentSpeedPitch_ = smoothedPitch_;
    }
    
    // Calculate wind occlusion cutoff
    if (params_.windOcclusionEnabled) {
        float targetCutoff = glm::mix(params_.occlusionMaxCutoff, params_.occlusionMinCutoff, 
                                       smoothedSpeed_);
        smoothedCutoff_ = glm::mix(smoothedCutoff_, targetCutoff, smoothFactor);
        currentOcclusionCutoff_ = smoothedCutoff_;
    }
    
    // Calculate wind ducking
    if (params_.windDuckingEnabled) {
        currentWindDuck_ = smoothedSpeed_ * params_.maxWindDuck;
    }
}

float SpeedAudioProcessor::calculateDopplerPitch(const glm::vec3& sourcePosition,
                                                   const glm::vec3& sourceVelocity) {
    if (!params_.dopplerEnabled) return 1.0f;
    
    glm::vec3 toSource = sourcePosition - playerPosition_;
    float distance = glm::length(toSource);
    if (distance < 0.001f) return 1.0f;
    
    glm::vec3 direction = toSource / distance;
    
    // Relative velocity along the line connecting source and listener
    float listenerApproachSpeed = glm::dot(playerVelocity_, direction);
    float sourceApproachSpeed = glm::dot(sourceVelocity, -direction);
    
    // Doppler formula: f' = f * (c + vr) / (c + vs)
    // where c = speed of sound, vr = receiver velocity, vs = source velocity
    float c = params_.speedOfSound;
    float dopplerRatio = (c + listenerApproachSpeed) / (c + sourceApproachSpeed);
    
    // Clamp to reasonable range and apply scale
    dopplerRatio = glm::clamp(dopplerRatio, 0.5f, 2.0f);
    return 1.0f + (dopplerRatio - 1.0f) * params_.dopplerScale;
}

void SpeedAudioProcessor::process(float* buffer, size_t frameCount) {
    if (!params_.windOcclusionEnabled) return;
    
    for (size_t i = 0; i < frameCount; ++i) {
        buffer[i * 2 + 0] = applyFilter(occlusionFilterL_, buffer[i * 2 + 0], currentOcclusionCutoff_);
        buffer[i * 2 + 1] = applyFilter(occlusionFilterR_, buffer[i * 2 + 1], currentOcclusionCutoff_);
    }
}

float SpeedAudioProcessor::applyFilter(FilterState& state, float input, float cutoff) {
    // Simple one-pole low-pass filter
    float omega = 2.0f * 3.14159f * cutoff / static_cast<float>(sampleRate_);
    float alpha = omega / (omega + 1.0f);
    
    float output = alpha * input + (1.0f - alpha) * state.y1;
    state.y1 = output;
    
    return output;
}

// ============================================================================
// GRANULAR SYNTHESIZER
// ============================================================================

GranularSynthesizer::GranularSynthesizer()
    : rng_(std::random_device{}()) {
}

GranularSynthesizer::~GranularSynthesizer() = default;

void GranularSynthesizer::initialize(uint32_t sampleRate) {
    sampleRate_ = sampleRate;
}

bool GranularSynthesizer::loadSource(const std::string& path) {
    // Audio loading would happen here
    // For now, return false - implementation depends on audio loader
    return false;
}

void GranularSynthesizer::setSourceBuffer(const std::vector<float>& buffer, 
                                           uint32_t sourceSampleRate) {
    sourceBuffer_ = buffer;
    sourceSampleRate_ = sourceSampleRate;
}

void GranularSynthesizer::start() {
    isPlaying_ = true;
    grainTimer_ = 0.0f;
}

void GranularSynthesizer::stop() {
    isPlaying_ = false;
    for (auto& grain : grains_) {
        grain.active = false;
    }
}

void GranularSynthesizer::synthesize(float* output, size_t frameCount) {
    std::memset(output, 0, frameCount * 2 * sizeof(float));
    
    if (!isPlaying_ || sourceBuffer_.empty()) return;
    
    float dt = 1.0f / static_cast<float>(sampleRate_);
    float grainInterval = 1.0f / params_.density;
    
    for (size_t i = 0; i < frameCount; ++i) {
        // Check if we need to spawn a new grain
        grainTimer_ += dt;
        if (grainTimer_ >= grainInterval) {
            grainTimer_ -= grainInterval;
            spawnGrain();
        }
        
        // Process active grains
        float left = 0.0f, right = 0.0f;
        
        for (auto& grain : grains_) {
            if (!grain.active) continue;
            
            // Get sample from source
            float sample = getSourceSample(grain.position);
            
            // Apply window
            float t = grain.elapsed / grain.duration;
            float window = windowFunction(t);
            
            sample *= window * grain.amplitude;
            
            left += sample * grain.panL;
            right += sample * grain.panR;
            
            // Advance grain
            grain.position += grain.pitch / static_cast<float>(sampleRate_);
            grain.elapsed += dt;
            
            if (grain.elapsed >= grain.duration) {
                grain.active = false;
            }
        }
        
        output[i * 2 + 0] += left;
        output[i * 2 + 1] += right;
    }
}

void GranularSynthesizer::spawnGrain() {
    // Find inactive grain slot
    Grain* grain = nullptr;
    for (auto& g : grains_) {
        if (!g.active) {
            grain = &g;
            break;
        }
    }
    if (!grain) return;
    
    // Initialize grain with randomization
    grain->active = true;
    grain->elapsed = 0.0f;
    
    grain->position = params_.position + (dist_(rng_) - 0.5f) * 2.0f * params_.positionVariation;
    grain->position = glm::clamp(grain->position, 0.0f, 1.0f);
    
    grain->duration = params_.duration + (dist_(rng_) - 0.5f) * 2.0f * params_.durationVariation;
    grain->duration = std::max(grain->duration, 0.001f);
    
    grain->pitch = params_.pitch + (dist_(rng_) - 0.5f) * 2.0f * params_.pitchVariation;
    
    float pan = params_.pan + (dist_(rng_) - 0.5f) * 2.0f * params_.panVariation;
    pan = glm::clamp(pan, -1.0f, 1.0f);
    grain->panL = std::sqrt(0.5f * (1.0f - pan));
    grain->panR = std::sqrt(0.5f * (1.0f + pan));
    
    grain->amplitude = params_.amplitude;
}

float GranularSynthesizer::getSourceSample(float position) {
    if (sourceBuffer_.empty()) return 0.0f;
    
    float samplePos = position * static_cast<float>(sourceBuffer_.size() - 1);
    size_t index0 = static_cast<size_t>(samplePos);
    size_t index1 = std::min(index0 + 1, sourceBuffer_.size() - 1);
    float frac = samplePos - static_cast<float>(index0);
    
    return sourceBuffer_[index0] * (1.0f - frac) + sourceBuffer_[index1] * frac;
}

float GranularSynthesizer::windowFunction(float t) {
    // Hann window
    return 0.5f * (1.0f - std::cos(2.0f * 3.14159f * t));
}

// ============================================================================
// FOOTSTEP SYNTHESIZER
// ============================================================================

FootstepSynthesizer::FootstepSynthesizer()
    : rng_(std::random_device{}()) {
}

FootstepSynthesizer::~FootstepSynthesizer() {
    shutdown();
}

void FootstepSynthesizer::initialize(uint32_t sampleRate) {
    sampleRate_ = sampleRate;
}

void FootstepSynthesizer::shutdown() {
    activeFootsteps_.clear();
}

void FootstepSynthesizer::triggerFootstep(const FootstepParams& params) {
    FootstepInstance step;
    step.params = params;
    step.elapsed = 0.0f;
    step.active = true;
    step.noisePhase = noiseDist_(rng_);
    
    // Duration based on surface
    switch (params.surface) {
        case SurfaceType::Concrete: step.duration = 0.08f; break;
        case SurfaceType::Grass: step.duration = 0.12f; break;
        case SurfaceType::Dirt: step.duration = 0.1f; break;
        case SurfaceType::Metal: step.duration = 0.15f; break;
        case SurfaceType::Wood: step.duration = 0.1f; break;
        case SurfaceType::Water: step.duration = 0.2f; break;
        case SurfaceType::Sand: step.duration = 0.15f; break;
        case SurfaceType::Gravel: step.duration = 0.12f; break;
        case SurfaceType::Snow: step.duration = 0.18f; break;
        case SurfaceType::Tile: step.duration = 0.07f; break;
    }
    
    activeFootsteps_.push_back(step);
}

void FootstepSynthesizer::synthesize(float* output, size_t frameCount) {
    std::memset(output, 0, frameCount * 2 * sizeof(float));
    
    float dt = 1.0f / static_cast<float>(sampleRate_);
    
    for (size_t i = 0; i < frameCount; ++i) {
        float sample = 0.0f;
        
        for (auto& step : activeFootsteps_) {
            if (!step.active) continue;
            
            sample += synthesizeFootstep(step, step.elapsed);
            step.elapsed += dt;
            
            if (step.elapsed >= step.duration) {
                step.active = false;
            }
        }
        
        output[i * 2 + 0] = sample;
        output[i * 2 + 1] = sample;
    }
    
    // Remove inactive footsteps
    activeFootsteps_.erase(
        std::remove_if(activeFootsteps_.begin(), activeFootsteps_.end(),
                       [](const FootstepInstance& s) { return !s.active; }),
        activeFootsteps_.end()
    );
}

float FootstepSynthesizer::synthesizeFootstep(FootstepInstance& step, float time) {
    float envelope = getSurfaceEnvelope(step.params.surface, time, step.duration);
    float noise = getSurfaceNoise(step.params.surface, step.noisePhase);
    
    // Add bass thump based on weight
    float bassFreq = 60.0f * step.params.weight;
    float bass = std::sin(time * bassFreq * 6.28318f) * 
                 std::exp(-time * 30.0f) * step.params.weight * 0.3f;
    
    // Combine
    float sample = (noise * envelope + bass) * step.params.intensity;
    
    // Add wetness (splash)
    if (step.params.wetness > 0.0f) {
        float splash = noiseDist_(rng_) * step.params.wetness * 
                      std::exp(-time * 15.0f) * 0.2f;
        sample += splash;
    }
    
    return glm::clamp(sample, -1.0f, 1.0f);
}

float FootstepSynthesizer::getSurfaceNoise(SurfaceType surface, float& phase) {
    float noise = noiseDist_(rng_);
    
    switch (surface) {
        case SurfaceType::Concrete:
        case SurfaceType::Tile:
            // Sharp, clicky
            return noise * 0.5f;
            
        case SurfaceType::Grass:
        case SurfaceType::Sand:
            // Soft, muffled
            phase += 0.1f;
            return noise * 0.3f + std::sin(phase * 10.0f) * 0.1f;
            
        case SurfaceType::Gravel:
            // Crunchy, lots of high frequency
            return noise * 0.8f + noiseDist_(rng_) * 0.2f;
            
        case SurfaceType::Metal:
            // Resonant
            phase += 0.05f;
            return noise * 0.4f + std::sin(phase * 50.0f) * 0.2f;
            
        case SurfaceType::Wood:
            // Hollow
            phase += 0.08f;
            return noise * 0.4f + std::sin(phase * 30.0f) * 0.15f;
            
        case SurfaceType::Water:
            // Splashy
            return noise * 0.6f;
            
        case SurfaceType::Dirt:
            // Dull thud
            return noise * 0.4f;
            
        case SurfaceType::Snow:
            // Crunchy but soft
            return noise * 0.35f + noiseDist_(rng_) * 0.15f;
            
        default:
            return noise * 0.5f;
    }
}

float FootstepSynthesizer::getSurfaceEnvelope(SurfaceType surface, float t, float duration) {
    float normalizedT = t / duration;
    
    switch (surface) {
        case SurfaceType::Concrete:
        case SurfaceType::Tile:
        case SurfaceType::Metal:
            // Sharp attack, quick decay
            return std::exp(-normalizedT * 8.0f);
            
        case SurfaceType::Grass:
        case SurfaceType::Sand:
        case SurfaceType::Snow:
            // Soft attack, slow decay
            return std::sin(normalizedT * 1.57f) * std::exp(-normalizedT * 3.0f);
            
        case SurfaceType::Water:
            // Multiple peaks (splash)
            return std::exp(-normalizedT * 2.0f) * 
                   (1.0f + 0.3f * std::sin(normalizedT * 20.0f));
            
        default:
            return std::exp(-normalizedT * 5.0f);
    }
}

// ============================================================================
// PROCEDURAL AUDIO MANAGER
// ============================================================================

ProceduralAudioManager& ProceduralAudioManager::getInstance() {
    static ProceduralAudioManager instance;
    return instance;
}

void ProceduralAudioManager::initialize(AudioSystem* audioSystem, uint32_t sampleRate) {
    audioSystem_ = audioSystem;
    sampleRate_ = sampleRate;
    
    windSynth_.initialize(sampleRate);
    dynamicMusic_.initialize(audioSystem);
    speedProcessor_.initialize(sampleRate);
    granularSynth_.initialize(sampleRate);
    footstepSynth_.initialize(sampleRate);
    
    // Allocate mixing buffers (enough for typical buffer sizes)
    windBuffer_.resize(4096 * 2);
    granularBuffer_.resize(4096 * 2);
    footstepBuffer_.resize(4096 * 2);
}

void ProceduralAudioManager::shutdown() {
    windSynth_.shutdown();
    dynamicMusic_.shutdown();
    speedProcessor_.shutdown();
    footstepSynth_.shutdown();
}

void ProceduralAudioManager::update(float deltaTime) {
    windSynth_.update(playerVelocity_, deltaTime);
    dynamicMusic_.update(deltaTime);
    speedProcessor_.update(playerPosition_, playerVelocity_, deltaTime);
}

void ProceduralAudioManager::updatePlayerState(const glm::vec3& position, 
                                                 const glm::vec3& velocity) {
    playerPosition_ = position;
    playerVelocity_ = velocity;
}

void ProceduralAudioManager::setGameState(GameMusicState state) {
    dynamicMusic_.setGameState(state);
}

void ProceduralAudioManager::triggerFootstep(SurfaceType surface, float intensity) {
    FootstepParams params;
    params.surface = surface;
    params.intensity = intensity;
    params.speed = glm::length(playerVelocity_);
    footstepSynth_.triggerFootstep(params);
}

void ProceduralAudioManager::synthesize(float* output, size_t frameCount) {
    // Ensure buffers are large enough
    if (windBuffer_.size() < frameCount * 2) {
        windBuffer_.resize(frameCount * 2);
        granularBuffer_.resize(frameCount * 2);
        footstepBuffer_.resize(frameCount * 2);
    }
    
    std::memset(output, 0, frameCount * 2 * sizeof(float));
    
    // Generate wind
    if (windEnabled_) {
        windSynth_.synthesize(windBuffer_.data(), frameCount);
        for (size_t i = 0; i < frameCount * 2; ++i) {
            output[i] += windBuffer_[i];
        }
    }
    
    // Generate granular sounds
    granularSynth_.synthesize(granularBuffer_.data(), frameCount);
    for (size_t i = 0; i < frameCount * 2; ++i) {
        output[i] += granularBuffer_[i];
    }
    
    // Generate footsteps
    footstepSynth_.synthesize(footstepBuffer_.data(), frameCount);
    for (size_t i = 0; i < frameCount * 2; ++i) {
        output[i] += footstepBuffer_[i];
    }
    
    // Apply speed-based effects
    if (speedEffectsEnabled_) {
        speedProcessor_.process(output, frameCount);
    }
}

} // namespace Sanic
