/**
 * AudioSystem.cpp
 * 
 * Implementation of the 3D audio system.
 * Uses miniaudio for cross-platform audio.
 */

#include "AudioSystem.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>

// We'll define the miniaudio implementation here
// In a real project, this would be in a separate file
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
// #include "miniaudio.h"

namespace Sanic {

// ============================================================================
// AUDIO CLIP
// ============================================================================

AudioClip::~AudioClip() {
    unload();
}

bool AudioClip::loadFromFile(const std::string& path) {
    filePath_ = path;
    
    // Check file extension
    std::string ext = path.substr(path.find_last_of('.') + 1);
    
    // For now, we'll just load WAV files
    // In a real implementation, this would use a library like dr_wav, stb_vorbis, etc.
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open audio file: " << path << std::endl;
        return false;
    }
    
    // Read WAV header (simplified)
    char riff[4];
    file.read(riff, 4);
    if (strncmp(riff, "RIFF", 4) != 0) {
        std::cerr << "Not a valid WAV file: " << path << std::endl;
        return false;
    }
    
    file.seekg(4, std::ios::cur);  // Skip file size
    
    char wave[4];
    file.read(wave, 4);
    if (strncmp(wave, "WAVE", 4) != 0) {
        std::cerr << "Not a valid WAV file: " << path << std::endl;
        return false;
    }
    
    // Find fmt chunk
    char chunkId[4];
    uint32_t chunkSize;
    
    while (file.read(chunkId, 4)) {
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        
        if (strncmp(chunkId, "fmt ", 4) == 0) {
            uint16_t audioFormat;
            file.read(reinterpret_cast<char*>(&audioFormat), 2);
            
            uint16_t numChannels;
            file.read(reinterpret_cast<char*>(&numChannels), 2);
            info_.channels = numChannels;
            
            file.read(reinterpret_cast<char*>(&info_.sampleRate), 4);
            
            file.seekg(6, std::ios::cur);  // Skip byte rate and block align
            
            uint16_t bitsPerSample;
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            info_.bitsPerSample = bitsPerSample;
            
            // Skip remaining fmt data
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        }
        else if (strncmp(chunkId, "data", 4) == 0) {
            // Read audio data
            size_t bytesPerSample = info_.bitsPerSample / 8;
            info_.sampleCount = chunkSize / bytesPerSample;
            
            std::vector<uint8_t> rawData(chunkSize);
            file.read(reinterpret_cast<char*>(rawData.data()), chunkSize);
            
            // Convert to float
            samples_.resize(info_.sampleCount);
            convertToFloat(rawData.data(), samples_.data(), info_.sampleCount,
                          info_.bitsPerSample, info_.channels);
            
            break;
        }
        else {
            // Skip unknown chunk
            file.seekg(chunkSize, std::ios::cur);
        }
    }
    
    info_.duration = static_cast<float>(info_.sampleCount) / 
                    static_cast<float>(info_.sampleRate * info_.channels);
    
    loaded_ = true;
    return true;
}

bool AudioClip::loadFromMemory(const void* data, size_t size, AudioClipInfo info) {
    info_ = info;
    
    const float* floatData = static_cast<const float*>(data);
    samples_.assign(floatData, floatData + size / sizeof(float));
    
    loaded_ = true;
    return true;
}

void AudioClip::unload() {
    samples_.clear();
    samples_.shrink_to_fit();
    loaded_ = false;
}

size_t AudioClip::streamSamples(float* output, size_t frameCount, size_t position) {
    if (!loaded_ || samples_.empty()) return 0;
    
    size_t samplesToRead = std::min(frameCount * info_.channels, 
                                   samples_.size() - position);
    
    if (samplesToRead > 0) {
        memcpy(output, samples_.data() + position, samplesToRead * sizeof(float));
    }
    
    return samplesToRead / info_.channels;
}

// ============================================================================
// AUDIO SOURCE
// ============================================================================

AudioSource::AudioSource() = default;

AudioSource::~AudioSource() {
    stop();
}

void AudioSource::setClip(std::shared_ptr<AudioClip> clip) {
    clip_ = clip;
    samplePosition_ = 0;
}

void AudioSource::setConfig(const AudioSourceConfig& config) {
    config_ = config;
}

void AudioSource::play() {
    if (!clip_) return;
    
    if (paused_) {
        paused_ = false;
    } else {
        samplePosition_ = 0;
    }
    playing_ = true;
}

void AudioSource::pause() {
    paused_ = true;
}

void AudioSource::stop() {
    playing_ = false;
    paused_ = false;
    samplePosition_ = 0;
}

void AudioSource::setTime(float time) {
    if (!clip_) return;
    
    const AudioClipInfo& info = clip_->getInfo();
    samplePosition_ = static_cast<size_t>(time * info.sampleRate * info.channels);
}

float AudioSource::getTime() const {
    if (!clip_) return 0.0f;
    
    const AudioClipInfo& info = clip_->getInfo();
    return static_cast<float>(samplePosition_) / 
           static_cast<float>(info.sampleRate * info.channels);
}

void AudioSource::setPosition(const glm::vec3& position) {
    position_ = position;
}

void AudioSource::setVelocity(const glm::vec3& velocity) {
    velocity_ = velocity;
}

void AudioSource::setDirection(const glm::vec3& direction) {
    direction_ = glm::normalize(direction);
}

void AudioSource::setVolume(float volume) {
    targetVolume_ = volume * config_.volume;
}

void AudioSource::setPitch(float pitch) {
    currentPitch_ = pitch * config_.pitch;
}

void AudioSource::updateInternal(float deltaTime, const glm::vec3& listenerPos,
                                 const glm::vec3& listenerForward, float occlusion) {
    if (!config_.is3D) {
        currentVolume_ = config_.volume;
        currentPan_ = 0.0f;
        return;
    }
    
    glm::vec3 toSource = position_ - listenerPos;
    float distance = glm::length(toSource);
    
    // Distance attenuation
    float attenuation = 1.0f;
    if (distance > config_.minDistance) {
        attenuation = config_.minDistance / 
                     (config_.minDistance + config_.rolloffFactor * 
                      (distance - config_.minDistance));
    }
    
    // Clamp to max distance
    if (distance > config_.maxDistance) {
        attenuation = 0.0f;
    }
    
    // Cone attenuation (directional sources)
    float coneAttenuation = 1.0f;
    if (config_.coneOuterAngle < 360.0f && distance > 0.001f) {
        glm::vec3 dirToListener = -toSource / distance;
        float angle = std::acos(glm::dot(direction_, dirToListener)) * 180.0f / 3.14159f;
        
        if (angle > config_.coneOuterAngle) {
            coneAttenuation = config_.coneOuterVolume;
        } else if (angle > config_.coneInnerAngle) {
            float t = (angle - config_.coneInnerAngle) / 
                     (config_.coneOuterAngle - config_.coneInnerAngle);
            coneAttenuation = glm::mix(1.0f, config_.coneOuterVolume, t);
        }
    }
    
    // Occlusion
    occlusionFactor_ = occlusion;
    float occlusionAttenuation = 1.0f - occlusionFactor_ * 0.8f;  // Max 80% reduction
    
    // Final volume
    targetVolume_ = config_.volume * attenuation * coneAttenuation * occlusionAttenuation;
    
    // Pan (stereo positioning)
    if (distance > 0.001f) {
        glm::vec3 dirNorm = toSource / distance;
        glm::vec3 right = glm::normalize(glm::cross(listenerForward, glm::vec3(0, 1, 0)));
        targetPan_ = glm::clamp(glm::dot(dirNorm, right), -1.0f, 1.0f);
    }
    
    // Smooth interpolation
    float smoothing = std::min(1.0f, deltaTime * 10.0f);
    currentVolume_ = glm::mix(currentVolume_, targetVolume_, smoothing);
    currentPan_ = glm::mix(currentPan_, targetPan_, smoothing);
}

size_t AudioSource::mixSamples(float* output, size_t frameCount, uint32_t sampleRate) {
    if (!clip_ || !playing_ || paused_) return 0;
    
    const std::vector<float>& samples = clip_->getSamples();
    const AudioClipInfo& info = clip_->getInfo();
    
    if (samples.empty()) return 0;
    
    size_t framesWritten = 0;
    
    for (size_t i = 0; i < frameCount; ++i) {
        if (samplePosition_ >= samples.size()) {
            if (config_.loop) {
                samplePosition_ = 0;
            } else {
                playing_ = false;
                break;
            }
        }
        
        float sample = 0.0f;
        
        if (info.channels == 1) {
            sample = samples[samplePosition_];
            samplePosition_++;
        } else {
            // Average stereo to mono for 3D spatialization
            sample = (samples[samplePosition_] + samples[samplePosition_ + 1]) * 0.5f;
            samplePosition_ += 2;
        }
        
        // Apply volume
        sample *= currentVolume_;
        
        // Apply pan for stereo output
        float leftGain = (currentPan_ <= 0.0f) ? 1.0f : (1.0f - currentPan_);
        float rightGain = (currentPan_ >= 0.0f) ? 1.0f : (1.0f + currentPan_);
        
        output[i * 2] += sample * leftGain;
        output[i * 2 + 1] += sample * rightGain;
        
        framesWritten++;
    }
    
    return framesWritten;
}

// ============================================================================
// REVERB ZONE
// ============================================================================

bool ReverbZone::containsPoint(const glm::vec3& point) const {
    glm::vec3 halfSize = size * 0.5f;
    glm::vec3 min = position - halfSize;
    glm::vec3 max = position + halfSize;
    
    return point.x >= min.x && point.x <= max.x &&
           point.y >= min.y && point.y <= max.y &&
           point.z >= min.z && point.z <= max.z;
}

float ReverbZone::getBlendWeight(const glm::vec3& point) const {
    glm::vec3 halfSize = size * 0.5f;
    glm::vec3 min = position - halfSize - glm::vec3(blendDistance);
    glm::vec3 max = position + halfSize + glm::vec3(blendDistance);
    
    if (point.x < min.x || point.x > max.x ||
        point.y < min.y || point.y > max.y ||
        point.z < min.z || point.z > max.z) {
        return 0.0f;
    }
    
    if (containsPoint(point)) {
        return 1.0f;
    }
    
    // Calculate blend based on distance to inner box
    glm::vec3 innerMin = position - halfSize;
    glm::vec3 innerMax = position + halfSize;
    
    float distX = std::max(innerMin.x - point.x, point.x - innerMax.x);
    float distY = std::max(innerMin.y - point.y, point.y - innerMax.y);
    float distZ = std::max(innerMin.z - point.z, point.z - innerMax.z);
    float maxDist = std::max({distX, distY, distZ, 0.0f});
    
    return 1.0f - (maxDist / blendDistance);
}

// ============================================================================
// AUDIO SYSTEM
// ============================================================================

AudioSystem::AudioSystem() {
    mixBuffer_.resize(4096 * 2);  // 4096 stereo frames
    reverbBuffer_.resize(4096 * 2);
    
    // Initialize delay lines for reverb
    for (int i = 0; i < 8; ++i) {
        delayLines_[i].resize(44100);  // 1 second max delay
    }
}

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::initialize() {
    if (initialized_) return true;
    
    // In a real implementation, this would initialize miniaudio
    // ma_device_config config = ma_device_config_init(ma_device_type_playback);
    // config.playback.format = ma_format_f32;
    // config.playback.channels = 2;
    // config.sampleRate = 44100;
    // config.dataCallback = audioCallback;
    // config.pUserData = this;
    
    std::cout << "Audio system initialized (stub)" << std::endl;
    initialized_ = true;
    return true;
}

void AudioSystem::shutdown() {
    if (!initialized_) return;
    
    // Stop all sources
    for (auto& source : sources_) {
        source->stop();
    }
    sources_.clear();
    
    // Unload all clips
    clipCache_.clear();
    
    // Shutdown miniaudio
    // ma_device_uninit(device_);
    
    initialized_ = false;
}

void AudioSystem::update(float deltaTime) {
    if (!initialized_) return;
    
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    
    // Update all sources
    for (auto& source : sources_) {
        float occlusion = 0.0f;
        if (occlusionEnabled_ && source->getPosition() != listener_.position) {
            occlusion = calculateOcclusion(source->getPosition());
        }
        
        source->updateInternal(deltaTime, listener_.position, 
                              listener_.forward, occlusion);
    }
    
    // Update one-shot sources
    for (auto it = oneShotSources_.begin(); it != oneShotSources_.end();) {
        it->lifetime -= deltaTime;
        if (it->lifetime <= 0 || !it->source->isPlaying()) {
            it = oneShotSources_.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioSystem::setListener(const AudioListener& listener) {
    listener_ = listener;
}

std::shared_ptr<AudioClip> AudioSystem::loadClip(const std::string& path) {
    std::lock_guard<std::mutex> lock(clipsMutex_);
    
    auto it = clipCache_.find(path);
    if (it != clipCache_.end()) {
        return it->second;
    }
    
    auto clip = std::make_shared<AudioClip>();
    if (!clip->loadFromFile(path)) {
        return nullptr;
    }
    
    clipCache_[path] = clip;
    return clip;
}

void AudioSystem::unloadClip(const std::string& path) {
    std::lock_guard<std::mutex> lock(clipsMutex_);
    clipCache_.erase(path);
}

std::shared_ptr<AudioSource> AudioSystem::createSource() {
    auto source = std::make_shared<AudioSource>();
    
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    sources_.push_back(source);
    
    return source;
}

void AudioSystem::destroySource(std::shared_ptr<AudioSource> source) {
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    
    auto it = std::find(sources_.begin(), sources_.end(), source);
    if (it != sources_.end()) {
        sources_.erase(it);
    }
}

void AudioSystem::playOneShot(const std::string& clipPath, const glm::vec3& position, float volume) {
    auto clip = loadClip(clipPath);
    if (clip) {
        playOneShot(clip, position, volume);
    }
}

void AudioSystem::playOneShot(std::shared_ptr<AudioClip> clip, const glm::vec3& position, float volume) {
    auto source = createSource();
    source->setClip(clip);
    source->setPosition(position);
    
    AudioSourceConfig config;
    config.volume = volume;
    config.loop = false;
    source->setConfig(config);
    
    source->play();
    
    OneShotSource oneShot;
    oneShot.source = source;
    oneShot.lifetime = clip->getInfo().duration + 0.5f;  // Extra buffer
    oneShotSources_.push_back(oneShot);
}

void AudioSystem::addReverbZone(std::shared_ptr<ReverbZone> zone) {
    reverbZones_.push_back(zone);
    
    // Sort by priority
    std::sort(reverbZones_.begin(), reverbZones_.end(),
        [](const auto& a, const auto& b) { return a->priority > b->priority; });
}

void AudioSystem::removeReverbZone(std::shared_ptr<ReverbZone> zone) {
    auto it = std::find(reverbZones_.begin(), reverbZones_.end(), zone);
    if (it != reverbZones_.end()) {
        reverbZones_.erase(it);
    }
}

void AudioSystem::setMasterVolume(float volume) {
    masterVolume_ = glm::clamp(volume, 0.0f, 1.0f);
}

void AudioSystem::setMusicVolume(float volume) {
    musicVolume_ = glm::clamp(volume, 0.0f, 1.0f);
}

void AudioSystem::setSFXVolume(float volume) {
    sfxVolume_ = glm::clamp(volume, 0.0f, 1.0f);
}

void AudioSystem::pauseAll() {
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    for (auto& source : sources_) {
        if (source->isPlaying()) {
            source->pause();
        }
    }
}

void AudioSystem::resumeAll() {
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    for (auto& source : sources_) {
        if (source->isPlaying()) {
            source->play();
        }
    }
}

void AudioSystem::setSDFTexture(VkImageView sdfView, VkSampler sdfSampler) {
    sdfView_ = sdfView;
    sdfSampler_ = sdfSampler;
}

AudioSystem::Stats AudioSystem::getStats() const {
    Stats stats = {};
    
    stats.activeSources = 0;
    stats.virtualSources = 0;
    
    for (const auto& source : sources_) {
        if (source->isPlaying()) {
            stats.activeSources++;
        }
    }
    
    stats.totalClipsLoaded = static_cast<uint32_t>(clipCache_.size());
    
    for (const auto& [path, clip] : clipCache_) {
        stats.memoryUsedBytes += clip->getSamples().size() * sizeof(float);
    }
    
    return stats;
}

void AudioSystem::audioCallback(void* userData, float* output, size_t frameCount) {
    AudioSystem* system = static_cast<AudioSystem*>(userData);
    system->processAudio(output, frameCount);
}

void AudioSystem::processAudio(float* output, size_t frameCount) {
    // Clear output buffer
    memset(output, 0, frameCount * channels_ * sizeof(float));
    
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    
    // Mix all sources
    for (auto& source : sources_) {
        if (source->isPlaying() && !source->isPaused()) {
            source->mixSamples(output, frameCount, sampleRate_);
        }
    }
    
    // Apply reverb
    if (!reverbZones_.empty()) {
        applyReverb(output, frameCount);
    }
    
    // Apply master volume
    for (size_t i = 0; i < frameCount * channels_; ++i) {
        output[i] *= masterVolume_;
        
        // Soft clipping
        if (output[i] > 1.0f) {
            output[i] = 1.0f - std::exp(-(output[i] - 1.0f));
        } else if (output[i] < -1.0f) {
            output[i] = -1.0f + std::exp(-(-output[i] - 1.0f));
        }
    }
}

float AudioSystem::calculateOcclusion(const glm::vec3& sourcePos) {
    // Simple ray march through SDF
    // In a full implementation, this would use the GPU SDF texture
    
    glm::vec3 dir = sourcePos - listener_.position;
    float dist = glm::length(dir);
    
    if (dist < 0.001f) return 0.0f;
    dir /= dist;
    
    // For now, return 0 (no occlusion)
    // Real implementation would march through SDF and accumulate occlusion
    return 0.0f;
}

void AudioSystem::applyReverb(float* buffer, size_t frameCount) {
    ReverbSettings settings = blendReverbSettings();
    
    if (settings.wetMix < 0.001f) return;
    
    // Simple Schroeder reverb
    static const float delayTimes[8] = {
        0.0297f, 0.0371f, 0.0411f, 0.0437f,
        0.0050f, 0.0077f, 0.0107f, 0.0131f
    };
    
    for (size_t i = 0; i < frameCount; ++i) {
        float inputL = buffer[i * 2];
        float inputR = buffer[i * 2 + 1];
        float input = (inputL + inputR) * 0.5f;
        
        float reverbL = 0.0f;
        float reverbR = 0.0f;
        
        // Comb filters (parallel)
        for (int j = 0; j < 4; ++j) {
            size_t delaySamples = static_cast<size_t>(delayTimes[j] * sampleRate_ * settings.roomSize);
            delaySamples = std::min(delaySamples, delayLines_[j].size() - 1);
            
            size_t readPos = (delayPositions_[j] + delayLines_[j].size() - delaySamples) % delayLines_[j].size();
            float delayed = delayLines_[j][readPos];
            
            float feedback = delayed * (1.0f - settings.damping * 0.4f);
            delayLines_[j][delayPositions_[j]] = input + feedback * 0.7f;
            delayPositions_[j] = (delayPositions_[j] + 1) % delayLines_[j].size();
            
            reverbL += delayed * (j % 2 == 0 ? 1.0f : 0.8f);
            reverbR += delayed * (j % 2 == 1 ? 1.0f : 0.8f);
        }
        
        // All-pass filters (series)
        for (int j = 4; j < 8; ++j) {
            size_t delaySamples = static_cast<size_t>(delayTimes[j] * sampleRate_);
            delaySamples = std::min(delaySamples, delayLines_[j].size() - 1);
            
            size_t readPos = (delayPositions_[j] + delayLines_[j].size() - delaySamples) % delayLines_[j].size();
            float delayed = delayLines_[j][readPos];
            
            float allpassInput = reverbL + reverbR;
            delayLines_[j][delayPositions_[j]] = allpassInput;
            delayPositions_[j] = (delayPositions_[j] + 1) % delayLines_[j].size();
            
            float allpassOutput = delayed - 0.5f * allpassInput;
            reverbL = reverbR = allpassOutput * 0.5f;
        }
        
        // Mix
        buffer[i * 2] = inputL * settings.dryMix + reverbL * settings.wetMix;
        buffer[i * 2 + 1] = inputR * settings.dryMix + reverbR * settings.wetMix;
    }
}

ReverbSettings AudioSystem::blendReverbSettings() {
    ReverbSettings result;
    result.wetMix = 0.0f;
    result.dryMix = 1.0f;
    
    float totalWeight = 0.0f;
    
    for (const auto& zone : reverbZones_) {
        float weight = zone->getBlendWeight(listener_.position);
        if (weight > 0.0f) {
            result.roomSize += zone->settings.roomSize * weight;
            result.damping += zone->settings.damping * weight;
            result.wetMix += zone->settings.wetMix * weight;
            result.dryMix += zone->settings.dryMix * weight;
            result.width += zone->settings.width * weight;
            result.decayTime += zone->settings.decayTime * weight;
            totalWeight += weight;
        }
    }
    
    if (totalWeight > 0.0f) {
        result.roomSize /= totalWeight;
        result.damping /= totalWeight;
        result.wetMix /= totalWeight;
        result.dryMix /= totalWeight;
        result.width /= totalWeight;
        result.decayTime /= totalWeight;
    }
    
    return result;
}

// ============================================================================
// AUDIO MIXER
// ============================================================================

AudioMixer::AudioMixer() {
    masterChannel_ = createChannel("Master");
}

AudioMixerChannel* AudioMixer::createChannel(const std::string& name, AudioMixerChannel* parent) {
    auto channel = std::make_unique<AudioMixerChannel>();
    channel->name = name;
    channel->parent = parent ? parent : masterChannel_;
    
    if (channel->parent) {
        channel->parent->children.push_back(channel.get());
    }
    
    channels_.push_back(std::move(channel));
    return channels_.back().get();
}

AudioMixerChannel* AudioMixer::getChannel(const std::string& name) {
    for (auto& channel : channels_) {
        if (channel->name == name) {
            return channel.get();
        }
    }
    return nullptr;
}

void AudioMixer::setChannelVolume(const std::string& name, float volume) {
    AudioMixerChannel* channel = getChannel(name);
    if (channel) {
        channel->volume = glm::clamp(volume, 0.0f, 1.0f);
    }
}

void AudioMixer::muteChannel(const std::string& name, bool muted) {
    AudioMixerChannel* channel = getChannel(name);
    if (channel) {
        channel->muted = muted;
    }
}

void AudioMixer::soloChannel(const std::string& name, bool solo) {
    AudioMixerChannel* channel = getChannel(name);
    if (channel) {
        channel->solo = solo;
    }
}

float AudioMixer::getEffectiveVolume(AudioMixerChannel* channel) {
    if (!channel || channel->muted) return 0.0f;
    
    float volume = channel->volume;
    
    AudioMixerChannel* parent = channel->parent;
    while (parent) {
        if (parent->muted) return 0.0f;
        volume *= parent->volume;
        parent = parent->parent;
    }
    
    return volume;
}

// ============================================================================
// AUDIO UTILITIES
// ============================================================================

void convertToFloat(const void* input, float* output, size_t sampleCount,
                   uint32_t bitsPerSample, uint32_t channels) {
    
    if (bitsPerSample == 16) {
        const int16_t* in = static_cast<const int16_t*>(input);
        for (size_t i = 0; i < sampleCount; ++i) {
            output[i] = static_cast<float>(in[i]) / 32768.0f;
        }
    }
    else if (bitsPerSample == 8) {
        const uint8_t* in = static_cast<const uint8_t*>(input);
        for (size_t i = 0; i < sampleCount; ++i) {
            output[i] = (static_cast<float>(in[i]) - 128.0f) / 128.0f;
        }
    }
    else if (bitsPerSample == 32) {
        const int32_t* in = static_cast<const int32_t*>(input);
        for (size_t i = 0; i < sampleCount; ++i) {
            output[i] = static_cast<float>(in[i]) / 2147483648.0f;
        }
    }
}

void convertFromFloat(const float* input, void* output, size_t sampleCount,
                     uint32_t bitsPerSample, uint32_t channels) {
    
    if (bitsPerSample == 16) {
        int16_t* out = static_cast<int16_t*>(output);
        for (size_t i = 0; i < sampleCount; ++i) {
            float sample = glm::clamp(input[i], -1.0f, 1.0f);
            out[i] = static_cast<int16_t>(sample * 32767.0f);
        }
    }
}

void resample(const float* input, size_t inputFrames, uint32_t inputRate,
              float* output, size_t outputFrames, uint32_t outputRate,
              uint32_t channels) {
    
    double ratio = static_cast<double>(inputFrames) / static_cast<double>(outputFrames);
    
    for (size_t i = 0; i < outputFrames; ++i) {
        double srcPos = i * ratio;
        size_t srcIdx = static_cast<size_t>(srcPos);
        double frac = srcPos - srcIdx;
        
        for (uint32_t c = 0; c < channels; ++c) {
            size_t idx = srcIdx * channels + c;
            size_t nextIdx = std::min((srcIdx + 1) * channels + c, (inputFrames - 1) * channels + c);
            
            output[i * channels + c] = static_cast<float>(
                input[idx] * (1.0 - frac) + input[nextIdx] * frac
            );
        }
    }
}

// HRTF stub implementation
HRTF& HRTF::getInstance() {
    static HRTF instance;
    return instance;
}

bool HRTF::loadDatabase(const std::string& path) {
    // Would load HRIR data from file
    return true;
}

HRTFData HRTF::getHRTF(float azimuth, float elevation) {
    HRTFData data;
    data.leftIR.resize(128, 0.0f);
    data.rightIR.resize(128, 0.0f);
    
    // Simple approximation: just delay and attenuate based on angle
    float pan = std::sin(azimuth);
    
    data.leftIR[0] = (1.0f - pan * 0.5f);
    data.rightIR[0] = (1.0f + pan * 0.5f);
    
    return data;
}

void HRTF::applyHRTF(const float* input, float* output, size_t frameCount,
                     float azimuth, float elevation) {
    HRTFData hrtf = getHRTF(azimuth, elevation);
    
    // Simple convolution (in reality, this would use FFT)
    for (size_t i = 0; i < frameCount; ++i) {
        output[i * 2] = input[i] * hrtf.leftIR[0];
        output[i * 2 + 1] = input[i] * hrtf.rightIR[0];
    }
}

float attenuateLinear(float distance, float minDist, float maxDist) {
    if (distance <= minDist) return 1.0f;
    if (distance >= maxDist) return 0.0f;
    return 1.0f - (distance - minDist) / (maxDist - minDist);
}

float attenuateInverse(float distance, float minDist, float rolloff) {
    if (distance <= minDist) return 1.0f;
    return minDist / (minDist + rolloff * (distance - minDist));
}

float attenuateExponential(float distance, float rolloff) {
    return std::exp(-rolloff * distance);
}

} // namespace Sanic
