/**
 * AudioAdvanced.cpp
 * 
 * Implementation of advanced audio features.
 */

#include "AudioAdvanced.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstring>

namespace Sanic {

// ============================================================================
// PLUGIN FACTORY REGISTRY
// ============================================================================

static std::vector<IAudioPluginFactory*> s_pluginFactories;

void IAudioPluginFactory::registerFactory(IAudioPluginFactory* factory) {
    s_pluginFactories.push_back(factory);
}

const std::vector<IAudioPluginFactory*>& IAudioPluginFactory::getFactories() {
    return s_pluginFactories;
}

IAudioPluginFactory* IAudioPluginFactory::findFactory(const std::string& name, EAudioPluginType type) {
    for (auto* factory : s_pluginFactories) {
        if (factory->getPluginName() == name && factory->getPluginType() == type) {
            return factory;
        }
    }
    return nullptr;
}

// ============================================================================
// CONVOLUTION REVERB
// ============================================================================

ConvolutionReverb::ConvolutionReverb() {
    overlapL_.resize(FFT_SIZE, 0.0f);
    overlapR_.resize(FFT_SIZE, 0.0f);
    inputBuffer_.resize(FFT_SIZE, 0.0f);
    fftBuffer_.resize(FFT_SIZE);
    timeBuffer_.resize(FFT_SIZE * 2);
}

ConvolutionReverb::~ConvolutionReverb() {
    shutdown();
}

bool ConvolutionReverb::initialize() {
    inputPos_ = 0;
    std::fill(overlapL_.begin(), overlapL_.end(), 0.0f);
    std::fill(overlapR_.begin(), overlapR_.end(), 0.0f);
    return true;
}

void ConvolutionReverb::shutdown() {
    irFreqL_.clear();
    irFreqR_.clear();
    irLength_ = 0;
    numPartitions_ = 0;
}

void ConvolutionReverb::setParams(const FReverbParams& params) {
    params_ = params;
}

bool ConvolutionReverb::loadImpulseResponse(const std::string& path) {
    // Load WAV file
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    
    // Read WAV header
    char header[44];
    file.read(header, 44);
    
    // Parse header
    uint16_t channels = *reinterpret_cast<uint16_t*>(&header[22]);
    uint32_t sampleRate = *reinterpret_cast<uint32_t*>(&header[24]);
    uint16_t bitsPerSample = *reinterpret_cast<uint16_t*>(&header[34]);
    uint32_t dataSize = *reinterpret_cast<uint32_t*>(&header[40]);
    
    size_t numSamples = dataSize / (bitsPerSample / 8);
    size_t samplesPerChannel = numSamples / channels;
    
    std::vector<float> irData(numSamples);
    
    if (bitsPerSample == 16) {
        std::vector<int16_t> rawData(numSamples);
        file.read(reinterpret_cast<char*>(rawData.data()), dataSize);
        for (size_t i = 0; i < numSamples; ++i) {
            irData[i] = rawData[i] / 32768.0f;
        }
    } else if (bitsPerSample == 32) {
        file.read(reinterpret_cast<char*>(irData.data()), dataSize);
    }
    
    return loadImpulseResponseFromMemory(irData.data(), samplesPerChannel, sampleRate, channels);
}

bool ConvolutionReverb::loadImpulseResponseFromMemory(const float* data, size_t sampleCount,
                                                       uint32_t sampleRate, uint32_t channels) {
    irLength_ = sampleCount;
    numPartitions_ = (irLength_ + HOP_SIZE - 1) / HOP_SIZE;
    
    // Calculate latency
    latencyMs_ = (float)HOP_SIZE / sampleRate * 1000.0f;
    
    // Allocate frequency domain storage for partitioned convolution
    irFreqL_.resize(numPartitions_ * (FFT_SIZE / 2 + 1));
    irFreqR_.resize(numPartitions_ * (FFT_SIZE / 2 + 1));
    
    // Convert each partition to frequency domain
    std::vector<float> partition(FFT_SIZE, 0.0f);
    std::vector<std::complex<float>> partitionFreq(FFT_SIZE / 2 + 1);
    
    for (size_t p = 0; p < numPartitions_; ++p) {
        size_t offset = p * HOP_SIZE;
        size_t count = std::min(HOP_SIZE, irLength_ - offset);
        
        // Left channel
        std::fill(partition.begin(), partition.end(), 0.0f);
        for (size_t i = 0; i < count; ++i) {
            size_t srcIdx = (offset + i) * channels;
            partition[i] = data[srcIdx] * irGain_;
        }
        
        // Zero-pad and FFT
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            fftBuffer_[i] = std::complex<float>(partition[i], 0.0f);
        }
        fft(fftBuffer_.data(), FFT_SIZE, false);
        
        // Store
        size_t freqOffset = p * (FFT_SIZE / 2 + 1);
        for (size_t i = 0; i <= FFT_SIZE / 2; ++i) {
            irFreqL_[freqOffset + i] = fftBuffer_[i];
        }
        
        // Right channel (or copy from left if mono)
        std::fill(partition.begin(), partition.end(), 0.0f);
        for (size_t i = 0; i < count; ++i) {
            size_t srcIdx = (offset + i) * channels + (channels > 1 ? 1 : 0);
            partition[i] = data[srcIdx] * irGain_;
        }
        
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            fftBuffer_[i] = std::complex<float>(partition[i], 0.0f);
        }
        fft(fftBuffer_.data(), FFT_SIZE, false);
        
        for (size_t i = 0; i <= FFT_SIZE / 2; ++i) {
            irFreqR_[freqOffset + i] = fftBuffer_[i];
        }
    }
    
    return true;
}

void ConvolutionReverb::process(float* buffer, size_t frameCount, uint32_t sampleRate) {
    if (irLength_ == 0 || numPartitions_ == 0) {
        // No IR loaded, pass through
        return;
    }
    
    float wet = params_.wetLevel;
    float dry = params_.dryLevel;
    
    // Process in chunks
    size_t processed = 0;
    while (processed < frameCount) {
        size_t toProcess = std::min(frameCount - processed, HOP_SIZE - inputPos_);
        
        // Copy input to buffer
        for (size_t i = 0; i < toProcess; ++i) {
            inputBuffer_[inputPos_ + i] = buffer[(processed + i) * 2]; // Left channel
        }
        inputPos_ += toProcess;
        
        if (inputPos_ >= HOP_SIZE) {
            // Process partition
            processPartitioned(inputBuffer_.data(), timeBuffer_.data(), HOP_SIZE);
            
            // Mix output
            for (size_t i = 0; i < HOP_SIZE && (processed + i) < frameCount; ++i) {
                float inL = buffer[(processed + i) * 2];
                float inR = buffer[(processed + i) * 2 + 1];
                
                buffer[(processed + i) * 2] = dry * inL + wet * timeBuffer_[i * 2];
                buffer[(processed + i) * 2 + 1] = dry * inR + wet * timeBuffer_[i * 2 + 1];
            }
            
            inputPos_ = 0;
        }
        
        processed += toProcess;
    }
}

void ConvolutionReverb::processPartitioned(const float* input, float* output, size_t frameCount) {
    // Simplified partitioned convolution
    // In production, use WOLA (Weighted Overlap-Add) or Uniformly Partitioned Convolution
    
    const size_t freqSize = FFT_SIZE / 2 + 1;
    std::vector<std::complex<float>> accL(freqSize, {0, 0});
    std::vector<std::complex<float>> accR(freqSize, {0, 0});
    
    // FFT input
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        fftBuffer_[i] = std::complex<float>(i < frameCount ? input[i] : 0.0f, 0.0f);
    }
    fft(fftBuffer_.data(), FFT_SIZE, false);
    
    // Multiply with first partition (simplified - full impl would use all partitions)
    for (size_t i = 0; i < freqSize; ++i) {
        accL[i] = fftBuffer_[i] * irFreqL_[i];
        accR[i] = fftBuffer_[i] * irFreqR_[i];
    }
    
    // IFFT left
    for (size_t i = 0; i < freqSize; ++i) {
        fftBuffer_[i] = accL[i];
    }
    for (size_t i = freqSize; i < FFT_SIZE; ++i) {
        fftBuffer_[i] = std::conj(fftBuffer_[FFT_SIZE - i]);
    }
    fft(fftBuffer_.data(), FFT_SIZE, true);
    
    // Overlap-add left
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        float sample = fftBuffer_[i].real() / FFT_SIZE + overlapL_[i];
        if (i < frameCount) {
            output[i * 2] = sample;
        }
        overlapL_[i] = (i + frameCount < FFT_SIZE) ? fftBuffer_[i + frameCount].real() / FFT_SIZE : 0.0f;
    }
    
    // IFFT right
    for (size_t i = 0; i < freqSize; ++i) {
        fftBuffer_[i] = accR[i];
    }
    for (size_t i = freqSize; i < FFT_SIZE; ++i) {
        fftBuffer_[i] = std::conj(fftBuffer_[FFT_SIZE - i]);
    }
    fft(fftBuffer_.data(), FFT_SIZE, true);
    
    // Overlap-add right
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        float sample = fftBuffer_[i].real() / FFT_SIZE + overlapR_[i];
        if (i < frameCount) {
            output[i * 2 + 1] = sample;
        }
        overlapR_[i] = (i + frameCount < FFT_SIZE) ? fftBuffer_[i + frameCount].real() / FFT_SIZE : 0.0f;
    }
}

void ConvolutionReverb::fft(std::complex<float>* data, size_t n, bool inverse) {
    // Cooley-Tukey FFT implementation
    // Bit-reversal permutation
    for (size_t i = 0, j = 0; i < n; ++i) {
        if (j > i) {
            std::swap(data[i], data[j]);
        }
        size_t m = n / 2;
        while (j >= m && m >= 1) {
            j -= m;
            m /= 2;
        }
        j += m;
    }
    
    // Danielson-Lanczos section
    for (size_t mmax = 1; mmax < n; mmax *= 2) {
        float theta = (inverse ? 1.0f : -1.0f) * 3.14159265358979f / mmax;
        std::complex<float> wphase(std::cos(theta), std::sin(theta));
        std::complex<float> w(1.0f, 0.0f);
        
        for (size_t m = 0; m < mmax; ++m) {
            for (size_t i = m; i < n; i += mmax * 2) {
                size_t j = i + mmax;
                std::complex<float> temp = w * data[j];
                data[j] = data[i] - temp;
                data[i] = data[i] + temp;
            }
            w *= wphase;
        }
    }
}

std::vector<std::string> ConvolutionReverb::getIRPresets() {
    return {
        "Small Room",
        "Medium Room",
        "Large Hall",
        "Cathedral",
        "Plate",
        "Spring",
        "Cave",
        "Outdoor"
    };
}

bool ConvolutionReverb::loadPreset(const std::string& presetName) {
    std::string path = "audio/impulses/" + presetName + ".wav";
    return loadImpulseResponse(path);
}

// ============================================================================
// FMOD INTEGRATION
// ============================================================================

bool FMODIntegration::initialize(const std::string& initPath) {
    // TODO: Initialize FMOD Studio system
    // FMOD::Studio::System::create(&studioSystem_);
    // studioSystem_->getCoreSystem(&coreSystem_);
    // studioSystem_->initialize(512, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr);
    
    initialized_ = true;
    return true;
}

void FMODIntegration::shutdown() {
    if (!initialized_) return;
    
    // TODO: Release FMOD systems
    // studioSystem_->release();
    
    activeEvents_.clear();
    initialized_ = false;
}

void FMODIntegration::update(float deltaTime) {
    if (!initialized_) return;
    
    // TODO: Update FMOD
    // studioSystem_->update();
}

bool FMODIntegration::loadBank(const std::string& bankPath) {
    if (!initialized_) return false;
    
    // TODO: Load FMOD bank
    // FMOD::Studio::Bank* bank;
    // studioSystem_->loadBankFile(bankPath.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
    
    return true;
}

void FMODIntegration::unloadBank(const std::string& bankPath) {
    // TODO: Unload FMOD bank
}

uint64_t FMODIntegration::playEvent(const std::string& eventPath, const glm::vec3& position) {
    if (!initialized_) return 0;
    
    uint64_t id = nextEventId_++;
    
    // TODO: Create and play FMOD event
    // FMOD::Studio::EventDescription* desc;
    // studioSystem_->getEvent(eventPath.c_str(), &desc);
    // FMOD::Studio::EventInstance* instance;
    // desc->createInstance(&instance);
    // instance->start();
    
    EventInstance event;
    event.id = id;
    activeEvents_[id] = event;
    
    set3DAttributes(id, position);
    
    return id;
}

void FMODIntegration::stopEvent(uint64_t eventId, bool immediate) {
    auto it = activeEvents_.find(eventId);
    if (it == activeEvents_.end()) return;
    
    // TODO: Stop FMOD event
    // it->second.instance->stop(immediate ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT);
    
    activeEvents_.erase(it);
}

void FMODIntegration::setEventParameter(uint64_t eventId, const std::string& paramName, float value) {
    auto it = activeEvents_.find(eventId);
    if (it == activeEvents_.end()) return;
    
    // TODO: Set FMOD parameter
    // it->second.instance->setParameterByName(paramName.c_str(), value);
}

void FMODIntegration::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                           const glm::vec3& up, const glm::vec3& velocity) {
    // TODO: Set FMOD listener attributes
    // FMOD_3D_ATTRIBUTES attributes;
    // attributes.position = {position.x, position.y, position.z};
    // attributes.velocity = {velocity.x, velocity.y, velocity.z};
    // attributes.forward = {forward.x, forward.y, forward.z};
    // attributes.up = {up.x, up.y, up.z};
    // studioSystem_->setListenerAttributes(0, &attributes);
}

void FMODIntegration::set3DAttributes(uint64_t eventId, const glm::vec3& position,
                                       const glm::vec3& velocity) {
    auto it = activeEvents_.find(eventId);
    if (it == activeEvents_.end()) return;
    
    // TODO: Set FMOD 3D attributes
    // FMOD_3D_ATTRIBUTES attributes = {};
    // attributes.position = {position.x, position.y, position.z};
    // attributes.velocity = {velocity.x, velocity.y, velocity.z};
    // it->second.instance->set3DAttributes(&attributes);
}

void FMODIntegration::setGlobalParameter(const std::string& paramName, float value) {
    // TODO: Set FMOD global parameter
    // studioSystem_->setParameterByName(paramName.c_str(), value);
}

float FMODIntegration::getGlobalParameter(const std::string& paramName) const {
    // TODO: Get FMOD global parameter
    // float value;
    // studioSystem_->getParameterByName(paramName.c_str(), &value);
    // return value;
    return 0.0f;
}

void FMODIntegration::setBusVolume(const std::string& busPath, float volume) {
    // TODO: Set FMOD bus volume
    // FMOD::Studio::Bus* bus;
    // studioSystem_->getBus(busPath.c_str(), &bus);
    // bus->setVolume(volume);
}

void FMODIntegration::setBusPaused(const std::string& busPath, bool paused) {
    // TODO: Set FMOD bus paused state
}

size_t FMODIntegration::getMemoryUsage() const {
    // TODO: Get FMOD memory stats
    return 0;
}

size_t FMODIntegration::getActiveEventCount() const {
    return activeEvents_.size();
}

void FMODIntegration::setDopplerScale(float scale) {
    // TODO: coreSystem_->set3DSettings(dopplerScale, distanceFactor, rolloffScale);
}

void FMODIntegration::setDistanceFactor(float factor) {
    // TODO: Update 3D settings
}

void FMODIntegration::setRolloffScale(float scale) {
    // TODO: Update 3D settings
}

// ============================================================================
// WWISE INTEGRATION
// ============================================================================

bool WwiseIntegration::initialize(const std::string& initPath) {
    // TODO: Initialize Wwise
    // AkMemSettings memSettings;
    // AK::MemoryMgr::Init(&memSettings);
    // AkStreamMgrSettings stmSettings;
    // AK::StreamMgr::Create(stmSettings);
    // AkInitSettings initSettings;
    // AkPlatformInitSettings platformSettings;
    // AK::SoundEngine::Init(&initSettings, &platformSettings);
    
    initialized_ = true;
    return true;
}

void WwiseIntegration::shutdown() {
    if (!initialized_) return;
    
    // TODO: Terminate Wwise
    // AK::SoundEngine::Term();
    // AK::StreamMgr::Term();
    // AK::MemoryMgr::Term();
    
    initialized_ = false;
}

void WwiseIntegration::update(float deltaTime) {
    if (!initialized_) return;
    
    // TODO: Render Wwise audio
    // AK::SoundEngine::RenderAudio();
}

bool WwiseIntegration::loadBank(const std::string& bankPath) {
    // TODO: Load Wwise bank
    // AkBankID bankID;
    // AK::SoundEngine::LoadBank(bankPath.c_str(), bankID);
    return true;
}

void WwiseIntegration::unloadBank(const std::string& bankPath) {
    // TODO: Unload Wwise bank
    // AK::SoundEngine::UnloadBank(bankPath.c_str());
}

uint64_t WwiseIntegration::playEvent(const std::string& eventPath, const glm::vec3& position) {
    if (!initialized_) return 0;
    
    uint64_t gameObjectId = nextGameObjectId_++;
    
    // TODO: Register game object and post event
    // AK::SoundEngine::RegisterGameObj(gameObjectId);
    // AK::SoundEngine::SetPosition(gameObjectId, position);
    // AkPlayingID playingId = AK::SoundEngine::PostEvent(eventPath.c_str(), gameObjectId);
    
    eventToGameObject_[gameObjectId] = gameObjectId;
    
    return gameObjectId;
}

void WwiseIntegration::stopEvent(uint64_t eventId, bool immediate) {
    auto it = eventToGameObject_.find(eventId);
    if (it == eventToGameObject_.end()) return;
    
    // TODO: Stop and unregister
    // AK::SoundEngine::StopAll(it->second);
    // AK::SoundEngine::UnregisterGameObj(it->second);
    
    eventToGameObject_.erase(it);
}

void WwiseIntegration::setEventParameter(uint64_t eventId, const std::string& paramName, float value) {
    setRTPCValue(paramName, value, eventId);
}

void WwiseIntegration::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                            const glm::vec3& up, const glm::vec3& velocity) {
    // TODO: Set Wwise listener position
    // AkListenerPosition listenerPos;
    // listenerPos.SetPosition(position);
    // listenerPos.SetOrientation(forward, up);
    // AK::SoundEngine::SetListenerPosition(listenerPos);
}

void WwiseIntegration::set3DAttributes(uint64_t eventId, const glm::vec3& position,
                                        const glm::vec3& velocity) {
    auto it = eventToGameObject_.find(eventId);
    if (it == eventToGameObject_.end()) return;
    
    // TODO: Set Wwise game object position
    // AkSoundPosition soundPos;
    // soundPos.SetPosition(position);
    // AK::SoundEngine::SetPosition(it->second, soundPos);
}

void WwiseIntegration::setGlobalParameter(const std::string& paramName, float value) {
    setRTPCValue(paramName, value, 0);
}

float WwiseIntegration::getGlobalParameter(const std::string& paramName) const {
    // TODO: Get RTPC value
    return 0.0f;
}

void WwiseIntegration::setBusVolume(const std::string& busPath, float volume) {
    // TODO: Set bus volume via RTPC
}

void WwiseIntegration::setBusPaused(const std::string& busPath, bool paused) {
    // TODO: Suspend/Resume rendering
}

size_t WwiseIntegration::getMemoryUsage() const {
    // TODO: Get Wwise memory stats
    return 0;
}

size_t WwiseIntegration::getActiveEventCount() const {
    return eventToGameObject_.size();
}

void WwiseIntegration::setRTPCValue(const std::string& rtpcName, float value, uint64_t gameObjectId) {
    // TODO: Set Wwise RTPC
    // AK::SoundEngine::SetRTPCValue(rtpcName.c_str(), value, gameObjectId);
}

void WwiseIntegration::setState(const std::string& stateGroup, const std::string& state) {
    // TODO: Set Wwise state
    // AK::SoundEngine::SetState(stateGroup.c_str(), state.c_str());
}

void WwiseIntegration::setSwitch(const std::string& switchGroup, const std::string& switchState,
                                  uint64_t gameObjectId) {
    // TODO: Set Wwise switch
    // AK::SoundEngine::SetSwitch(switchGroup.c_str(), switchState.c_str(), gameObjectId);
}

void WwiseIntegration::postTrigger(const std::string& triggerName, uint64_t gameObjectId) {
    // TODO: Post Wwise trigger
    // AK::SoundEngine::PostTrigger(triggerName.c_str(), gameObjectId);
}

// ============================================================================
// GPU AUDIO OCCLUSION
// ============================================================================

GPUAudioOcclusion::GPUAudioOcclusion(VulkanContext& context) : context_(context) {
}

GPUAudioOcclusion::~GPUAudioOcclusion() {
    shutdown();
}

bool GPUAudioOcclusion::initialize() {
    createPipelines();
    return true;
}

void GPUAudioOcclusion::shutdown() {
    // TODO: Cleanup Vulkan resources
}

void GPUAudioOcclusion::calculateOcclusion(FOcclusionParams& params) {
    // CPU fallback using SDF marching
    marchSDF(params.sourcePosition, params.listenerPosition, params.directOcclusion);
    params.reverbOcclusion = params.directOcclusion * 0.5f;
}

void GPUAudioOcclusion::calculateOcclusionBatch(std::vector<FOcclusionParams>& params) {
    // For small batches, use CPU
    if (params.size() < 16) {
        for (auto& p : params) {
            calculateOcclusion(p);
        }
        return;
    }
    
    // TODO: Use GPU for large batches
}

void GPUAudioOcclusion::calculateOcclusionGPU(VkCommandBuffer cmd, std::vector<FOcclusionParams>& params) {
    // TODO: Dispatch compute shader for occlusion calculation
}

void GPUAudioOcclusion::setSDF(VkImageView sdfView, VkSampler sdfSampler,
                                const glm::vec3& sdfOrigin, const glm::vec3& sdfSize) {
    sdfView_ = sdfView;
    sdfSampler_ = sdfSampler;
    sdfOrigin_ = sdfOrigin;
    sdfSize_ = sdfSize;
}

void GPUAudioOcclusion::setAccelerationStructure(VkAccelerationStructureKHR tlas) {
    tlas_ = tlas;
}

void GPUAudioOcclusion::createPipelines() {
    // TODO: Create compute pipelines for SDF and RT occlusion
}

void GPUAudioOcclusion::marchSDF(const glm::vec3& from, const glm::vec3& to, float& occlusion) {
    // Simple SDF marching for occlusion
    // In production, sample actual SDF texture
    
    glm::vec3 dir = to - from;
    float distance = glm::length(dir);
    dir /= distance;
    
    float accumulated = 0.0f;
    float t = 0.0f;
    const float stepSize = 0.1f;
    
    while (t < distance) {
        glm::vec3 pos = from + dir * t;
        
        // Sample SDF (placeholder - assumes open space)
        float sdfValue = 1.0f; // Would sample actual SDF here
        
        if (sdfValue < 0.0f) {
            accumulated += stepSize;
        }
        
        t += std::max(stepSize, sdfValue);
    }
    
    // Convert accumulated distance to occlusion factor
    occlusion = glm::clamp(accumulated / distance, 0.0f, 1.0f);
}

// ============================================================================
// AMBISONICS
// ============================================================================

AmbisonicsEncoder::AmbisonicsEncoder(Order order) : order_(order) {
    shCoefficients_.resize(getChannelCount());
}

void AmbisonicsEncoder::encode(const float* input, float* output, size_t frameCount,
                                float azimuth, float elevation, float distance) {
    computeSHCoefficients(azimuth, elevation);
    
    int channels = getChannelCount();
    float attenuation = 1.0f / std::max(1.0f, distance);
    
    // Clear output
    std::memset(output, 0, frameCount * channels * sizeof(float));
    
    // Encode each sample
    for (size_t i = 0; i < frameCount; ++i) {
        float sample = input[i] * attenuation;
        for (int c = 0; c < channels; ++c) {
            output[i * channels + c] = sample * shCoefficients_[c];
        }
    }
}

void AmbisonicsEncoder::decodeBinaural(const float* input, float* output, size_t frameCount) {
    int channels = getChannelCount();
    
    // Simple first-order ambisonics to binaural decode
    // W, X, Y, Z channels
    for (size_t i = 0; i < frameCount; ++i) {
        float w = input[i * channels + 0];
        float x = channels > 1 ? input[i * channels + 1] : 0.0f;
        float y = channels > 2 ? input[i * channels + 2] : 0.0f;
        
        // Basic stereo decode (proper implementation would use HRTF)
        output[i * 2 + 0] = w * 0.707f + x * 0.5f - y * 0.5f;  // Left
        output[i * 2 + 1] = w * 0.707f + x * 0.5f + y * 0.5f;  // Right
    }
}

void AmbisonicsEncoder::decodeSpeakers(const float* input, float* output, size_t frameCount,
                                        const std::vector<glm::vec2>& speakerPositions) {
    int channels = getChannelCount();
    size_t numSpeakers = speakerPositions.size();
    
    // Decode to each speaker based on position
    for (size_t i = 0; i < frameCount; ++i) {
        for (size_t s = 0; s < numSpeakers; ++s) {
            float azimuth = speakerPositions[s].x;
            float elevation = speakerPositions[s].y;
            computeSHCoefficients(azimuth, elevation);
            
            float sample = 0.0f;
            for (int c = 0; c < channels; ++c) {
                sample += input[i * channels + c] * shCoefficients_[c];
            }
            output[i * numSpeakers + s] = sample;
        }
    }
}

int AmbisonicsEncoder::getChannelCount() const {
    return ((int)order_ + 1) * ((int)order_ + 1);
}

void AmbisonicsEncoder::computeSHCoefficients(float azimuth, float elevation) {
    // Spherical harmonics coefficients
    float cosElev = std::cos(elevation);
    float sinElev = std::sin(elevation);
    float cosAzim = std::cos(azimuth);
    float sinAzim = std::sin(azimuth);
    
    // Order 0 (omnidirectional)
    shCoefficients_[0] = 1.0f;  // W
    
    if ((int)order_ >= 1) {
        // Order 1
        shCoefficients_[1] = cosAzim * cosElev;  // X (front-back)
        shCoefficients_[2] = sinAzim * cosElev;  // Y (left-right)
        shCoefficients_[3] = sinElev;             // Z (up-down)
    }
    
    if ((int)order_ >= 2) {
        // Order 2
        float cos2Azim = std::cos(2.0f * azimuth);
        float sin2Azim = std::sin(2.0f * azimuth);
        
        shCoefficients_[4] = 0.5f * (3.0f * sinElev * sinElev - 1.0f);
        shCoefficients_[5] = cosAzim * sinElev * cosElev;
        shCoefficients_[6] = sinAzim * sinElev * cosElev;
        shCoefficients_[7] = cos2Azim * cosElev * cosElev;
        shCoefficients_[8] = sin2Azim * cosElev * cosElev;
    }
    
    // Order 3 would add 7 more channels...
}

// ============================================================================
// DSP EFFECTS
// ============================================================================

void LowPassFilter::process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) {
    if (bypass) return;
    
    // Simple one-pole low-pass filter
    float dt = 1.0f / sampleRate;
    float rc = 1.0f / (2.0f * 3.14159f * cutoffFreq_);
    float alpha = dt / (rc + dt);
    
    for (size_t i = 0; i < frameCount; ++i) {
        for (uint32_t c = 0; c < channels; ++c) {
            float sample = buffer[i * channels + c];
            sample = prevSamples_[c][0] + alpha * (sample - prevSamples_[c][0]);
            prevSamples_[c][0] = sample;
            
            buffer[i * channels + c] = sample * mix + buffer[i * channels + c] * (1.0f - mix);
        }
    }
}

void LowPassFilter::reset() {
    std::memset(prevSamples_, 0, sizeof(prevSamples_));
}

void HighPassFilter::process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) {
    if (bypass) return;
    
    float dt = 1.0f / sampleRate;
    float rc = 1.0f / (2.0f * 3.14159f * cutoffFreq_);
    float alpha = rc / (rc + dt);
    
    for (size_t i = 0; i < frameCount; ++i) {
        for (uint32_t c = 0; c < channels; ++c) {
            float sample = buffer[i * channels + c];
            float filtered = alpha * (prevSamples_[c][1] + sample - prevSamples_[c][0]);
            prevSamples_[c][0] = sample;
            prevSamples_[c][1] = filtered;
            
            buffer[i * channels + c] = filtered * mix + buffer[i * channels + c] * (1.0f - mix);
        }
    }
}

void HighPassFilter::reset() {
    std::memset(prevSamples_, 0, sizeof(prevSamples_));
}

void Compressor::process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) {
    if (bypass) return;
    
    float threshold = std::pow(10.0f, thresholdDb_ / 20.0f);
    float makeupGain = std::pow(10.0f, makeupGainDb_ / 20.0f);
    float attackCoef = std::exp(-1.0f / (attackMs_ * 0.001f * sampleRate));
    float releaseCoef = std::exp(-1.0f / (releaseMs_ * 0.001f * sampleRate));
    
    for (size_t i = 0; i < frameCount; ++i) {
        // Find peak across channels
        float peak = 0.0f;
        for (uint32_t c = 0; c < channels; ++c) {
            peak = std::max(peak, std::abs(buffer[i * channels + c]));
        }
        
        // Update envelope
        if (peak > envelope_) {
            envelope_ = attackCoef * envelope_ + (1.0f - attackCoef) * peak;
        } else {
            envelope_ = releaseCoef * envelope_ + (1.0f - releaseCoef) * peak;
        }
        
        // Calculate gain reduction
        float gain = 1.0f;
        if (envelope_ > threshold) {
            float overDb = 20.0f * std::log10(envelope_ / threshold);
            float reducedDb = overDb * (1.0f - 1.0f / ratio_);
            gain = std::pow(10.0f, -reducedDb / 20.0f);
        }
        
        // Apply gain
        for (uint32_t c = 0; c < channels; ++c) {
            buffer[i * channels + c] *= gain * makeupGain;
        }
    }
}

void Compressor::reset() {
    envelope_ = 0.0f;
}

void Limiter::process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) {
    if (bypass) return;
    
    float threshold = std::pow(10.0f, thresholdDb_ / 20.0f);
    float releaseCoef = std::exp(-1.0f / (releaseMs_ * 0.001f * sampleRate));
    
    for (size_t i = 0; i < frameCount; ++i) {
        // Find peak
        float peak = 0.0f;
        for (uint32_t c = 0; c < channels; ++c) {
            peak = std::max(peak, std::abs(buffer[i * channels + c]));
        }
        
        // Calculate required gain
        float targetGain = (peak > threshold) ? threshold / peak : 1.0f;
        
        // Smooth gain changes
        if (targetGain < gain_) {
            gain_ = targetGain;  // Instant attack
        } else {
            gain_ = releaseCoef * gain_ + (1.0f - releaseCoef) * targetGain;
        }
        
        // Apply
        for (uint32_t c = 0; c < channels; ++c) {
            buffer[i * channels + c] *= gain_;
        }
    }
}

void Limiter::reset() {
    gain_ = 1.0f;
}

void Delay::process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) {
    if (bypass) return;
    
    size_t delaySamples = (size_t)(delayTimeMs_ * 0.001f * sampleRate) * channels;
    
    if (delayBuffer_.size() != delaySamples) {
        delayBuffer_.resize(delaySamples, 0.0f);
        delayPos_ = 0;
    }
    
    for (size_t i = 0; i < frameCount; ++i) {
        for (uint32_t c = 0; c < channels; ++c) {
            size_t idx = i * channels + c;
            size_t delayIdx = (delayPos_ + c) % delayBuffer_.size();
            
            float delayed = delayBuffer_[delayIdx];
            float input = buffer[idx];
            
            delayBuffer_[delayIdx] = input + delayed * feedback_;
            buffer[idx] = input * (1.0f - mix) + delayed * mix;
        }
        
        delayPos_ = (delayPos_ + channels) % delayBuffer_.size();
    }
}

void Delay::reset() {
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    delayPos_ = 0;
}

void Chorus::process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) {
    if (bypass) return;
    
    // Delay buffer for modulation
    size_t maxDelaySamples = (size_t)(30.0f * 0.001f * sampleRate) * channels;
    if (delayBuffer_.size() != maxDelaySamples) {
        delayBuffer_.resize(maxDelaySamples, 0.0f);
        writePos_ = 0;
    }
    
    float phaseIncrement = rate_ / sampleRate;
    
    for (size_t i = 0; i < frameCount; ++i) {
        float lfo = std::sin(phase_ * 2.0f * 3.14159f);
        phase_ += phaseIncrement;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        
        float delayMs = 10.0f + lfo * depth_ * 10.0f;
        size_t delaySamples = (size_t)(delayMs * 0.001f * sampleRate);
        
        for (uint32_t c = 0; c < channels; ++c) {
            size_t idx = i * channels + c;
            size_t writeIdx = (writePos_ + c) % delayBuffer_.size();
            
            delayBuffer_[writeIdx] = buffer[idx];
            
            size_t readIdx = (writePos_ + delayBuffer_.size() - delaySamples * channels + c) % delayBuffer_.size();
            float delayed = delayBuffer_[readIdx];
            
            buffer[idx] = buffer[idx] * 0.5f + delayed * 0.5f;
        }
        
        writePos_ = (writePos_ + channels) % delayBuffer_.size();
    }
}

void Chorus::reset() {
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    writePos_ = 0;
    phase_ = 0.0f;
}

// ============================================================================
// EFFECTS CHAIN
// ============================================================================

void EffectsChain::addEffect(std::unique_ptr<IAudioEffect> effect) {
    effects_.push_back(std::move(effect));
}

void EffectsChain::removeEffect(size_t index) {
    if (index < effects_.size()) {
        effects_.erase(effects_.begin() + index);
    }
}

void EffectsChain::process(float* buffer, size_t frameCount, uint32_t channels, uint32_t sampleRate) {
    for (auto& effect : effects_) {
        if (!effect->bypass) {
            effect->process(buffer, frameCount, channels, sampleRate);
        }
    }
}

void EffectsChain::reset() {
    for (auto& effect : effects_) {
        effect->reset();
    }
}

IAudioEffect* EffectsChain::getEffect(size_t index) {
    return index < effects_.size() ? effects_[index].get() : nullptr;
}

} // namespace Sanic
