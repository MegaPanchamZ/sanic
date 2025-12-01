/**
 * WeatherSystem.cpp
 * 
 * Implementation of Weather and Time of Day system
 */

#include "WeatherSystem.h"
#include <algorithm>
#include <random>
#include <cmath>

namespace Sanic {

// ============================================================================
// WEATHER PARAMETERS
// ============================================================================

WeatherParameters WeatherParameters::lerp(const WeatherParameters& a, 
                                           const WeatherParameters& b, 
                                           float t) {
    WeatherParameters result;
    t = glm::clamp(t, 0.0f, 1.0f);
    
    result.cloudCoverage = glm::mix(a.cloudCoverage, b.cloudCoverage, t);
    result.cloudDensity = glm::mix(a.cloudDensity, b.cloudDensity, t);
    result.cloudSpeed = glm::mix(a.cloudSpeed, b.cloudSpeed, t);
    result.cloudColor = glm::mix(a.cloudColor, b.cloudColor, t);
    
    result.fogDensity = glm::mix(a.fogDensity, b.fogDensity, t);
    result.fogHeight = glm::mix(a.fogHeight, b.fogHeight, t);
    result.fogColor = glm::mix(a.fogColor, b.fogColor, t);
    
    result.precipitationIntensity = glm::mix(a.precipitationIntensity, b.precipitationIntensity, t);
    result.precipitationSize = glm::mix(a.precipitationSize, b.precipitationSize, t);
    result.precipitationColor = glm::mix(a.precipitationColor, b.precipitationColor, t);
    
    result.windDirection = glm::normalize(glm::mix(a.windDirection, b.windDirection, t));
    result.windSpeed = glm::mix(a.windSpeed, b.windSpeed, t);
    result.windGustStrength = glm::mix(a.windGustStrength, b.windGustStrength, t);
    result.windGustFrequency = glm::mix(a.windGustFrequency, b.windGustFrequency, t);
    
    result.lightningFrequency = glm::mix(a.lightningFrequency, b.lightningFrequency, t);
    result.lightningIntensity = glm::mix(a.lightningIntensity, b.lightningIntensity, t);
    
    result.temperature = glm::mix(a.temperature, b.temperature, t);
    result.humidity = glm::mix(a.humidity, b.humidity, t);
    result.visibility = glm::mix(a.visibility, b.visibility, t);
    result.ambientVolume = glm::mix(a.ambientVolume, b.ambientVolume, t);
    
    // Use target state and sound when past halfway
    result.state = (t < 0.5f) ? a.state : b.state;
    result.ambientSound = (t < 0.5f) ? a.ambientSound : b.ambientSound;
    
    return result;
}

// ============================================================================
// WEATHER PRESETS
// ============================================================================

WeatherParameters WeatherPresets::Clear() {
    WeatherParameters params;
    params.state = WeatherState::Clear;
    params.cloudCoverage = 0.1f;
    params.cloudDensity = 0.3f;
    params.cloudSpeed = 0.05f;
    params.fogDensity = 0.0001f;
    params.visibility = 10000.0f;
    params.windSpeed = 2.0f;
    params.temperature = 22.0f;
    params.humidity = 0.4f;
    return params;
}

WeatherParameters WeatherPresets::Cloudy() {
    WeatherParameters params;
    params.state = WeatherState::Cloudy;
    params.cloudCoverage = 0.5f;
    params.cloudDensity = 0.5f;
    params.cloudSpeed = 0.1f;
    params.fogDensity = 0.0002f;
    params.visibility = 8000.0f;
    params.windSpeed = 5.0f;
    params.temperature = 18.0f;
    params.humidity = 0.6f;
    return params;
}

WeatherParameters WeatherPresets::Overcast() {
    WeatherParameters params;
    params.state = WeatherState::Overcast;
    params.cloudCoverage = 0.95f;
    params.cloudDensity = 0.8f;
    params.cloudSpeed = 0.15f;
    params.cloudColor = glm::vec3(0.6f);
    params.fogDensity = 0.0005f;
    params.visibility = 5000.0f;
    params.windSpeed = 8.0f;
    params.temperature = 15.0f;
    params.humidity = 0.75f;
    return params;
}

WeatherParameters WeatherPresets::Fog() {
    WeatherParameters params;
    params.state = WeatherState::Fog;
    params.cloudCoverage = 0.3f;
    params.fogDensity = 0.01f;
    params.fogHeight = 50.0f;
    params.fogColor = glm::vec3(0.8f, 0.85f, 0.9f);
    params.visibility = 100.0f;
    params.windSpeed = 1.0f;
    params.temperature = 10.0f;
    params.humidity = 0.95f;
    params.ambientSound = "ambient/fog";
    return params;
}

WeatherParameters WeatherPresets::LightRain() {
    WeatherParameters params;
    params.state = WeatherState::LightRain;
    params.cloudCoverage = 0.7f;
    params.cloudDensity = 0.6f;
    params.cloudColor = glm::vec3(0.7f);
    params.fogDensity = 0.001f;
    params.precipitationIntensity = 0.3f;
    params.precipitationSize = 0.8f;
    params.precipitationColor = glm::vec3(0.8f, 0.85f, 0.9f);
    params.visibility = 3000.0f;
    params.windSpeed = 6.0f;
    params.temperature = 14.0f;
    params.humidity = 0.85f;
    params.ambientSound = "ambient/light_rain";
    return params;
}

WeatherParameters WeatherPresets::Rain() {
    WeatherParameters params;
    params.state = WeatherState::Rain;
    params.cloudCoverage = 0.9f;
    params.cloudDensity = 0.7f;
    params.cloudColor = glm::vec3(0.5f);
    params.fogDensity = 0.002f;
    params.precipitationIntensity = 0.6f;
    params.precipitationSize = 1.0f;
    params.visibility = 1500.0f;
    params.windSpeed = 10.0f;
    params.windGustStrength = 0.3f;
    params.temperature = 12.0f;
    params.humidity = 0.9f;
    params.ambientSound = "ambient/rain";
    return params;
}

WeatherParameters WeatherPresets::HeavyRain() {
    WeatherParameters params;
    params.state = WeatherState::HeavyRain;
    params.cloudCoverage = 1.0f;
    params.cloudDensity = 0.9f;
    params.cloudColor = glm::vec3(0.3f);
    params.fogDensity = 0.005f;
    params.precipitationIntensity = 1.0f;
    params.precipitationSize = 1.5f;
    params.visibility = 500.0f;
    params.windSpeed = 15.0f;
    params.windGustStrength = 0.5f;
    params.windGustFrequency = 0.5f;
    params.temperature = 10.0f;
    params.humidity = 0.95f;
    params.ambientSound = "ambient/heavy_rain";
    return params;
}

WeatherParameters WeatherPresets::Thunderstorm() {
    WeatherParameters params;
    params.state = WeatherState::Thunderstorm;
    params.cloudCoverage = 1.0f;
    params.cloudDensity = 1.0f;
    params.cloudColor = glm::vec3(0.2f, 0.2f, 0.25f);
    params.fogDensity = 0.003f;
    params.precipitationIntensity = 0.9f;
    params.precipitationSize = 1.2f;
    params.visibility = 300.0f;
    params.windSpeed = 25.0f;
    params.windGustStrength = 0.8f;
    params.windGustFrequency = 0.7f;
    params.lightningFrequency = 0.1f;
    params.lightningIntensity = 1.0f;
    params.temperature = 8.0f;
    params.humidity = 0.98f;
    params.ambientSound = "ambient/thunderstorm";
    return params;
}

WeatherParameters WeatherPresets::LightSnow() {
    WeatherParameters params;
    params.state = WeatherState::LightSnow;
    params.cloudCoverage = 0.8f;
    params.cloudDensity = 0.5f;
    params.cloudColor = glm::vec3(0.9f);
    params.fogDensity = 0.001f;
    params.precipitationIntensity = 0.3f;
    params.precipitationSize = 1.5f;
    params.precipitationColor = glm::vec3(1.0f);
    params.visibility = 2000.0f;
    params.windSpeed = 3.0f;
    params.temperature = -2.0f;
    params.humidity = 0.7f;
    params.ambientSound = "ambient/light_snow";
    return params;
}

WeatherParameters WeatherPresets::Snow() {
    WeatherParameters params;
    params.state = WeatherState::Snow;
    params.cloudCoverage = 0.95f;
    params.cloudDensity = 0.7f;
    params.cloudColor = glm::vec3(0.85f);
    params.fogDensity = 0.003f;
    params.precipitationIntensity = 0.6f;
    params.precipitationSize = 2.0f;
    params.precipitationColor = glm::vec3(1.0f);
    params.visibility = 800.0f;
    params.windSpeed = 8.0f;
    params.temperature = -5.0f;
    params.humidity = 0.75f;
    params.ambientSound = "ambient/snow";
    return params;
}

WeatherParameters WeatherPresets::Blizzard() {
    WeatherParameters params;
    params.state = WeatherState::Blizzard;
    params.cloudCoverage = 1.0f;
    params.cloudDensity = 1.0f;
    params.cloudColor = glm::vec3(0.8f);
    params.fogDensity = 0.02f;
    params.fogColor = glm::vec3(0.95f);
    params.precipitationIntensity = 1.0f;
    params.precipitationSize = 2.5f;
    params.precipitationColor = glm::vec3(1.0f);
    params.visibility = 50.0f;
    params.windSpeed = 35.0f;
    params.windGustStrength = 0.6f;
    params.windGustFrequency = 0.8f;
    params.temperature = -15.0f;
    params.humidity = 0.8f;
    params.ambientSound = "ambient/blizzard";
    return params;
}

WeatherParameters WeatherPresets::Sandstorm() {
    WeatherParameters params;
    params.state = WeatherState::Sandstorm;
    params.cloudCoverage = 0.3f;
    params.fogDensity = 0.015f;
    params.fogColor = glm::vec3(0.8f, 0.6f, 0.3f);
    params.visibility = 100.0f;
    params.windSpeed = 30.0f;
    params.windGustStrength = 0.7f;
    params.windGustFrequency = 0.6f;
    params.temperature = 35.0f;
    params.humidity = 0.1f;
    params.ambientSound = "ambient/sandstorm";
    return params;
}

WeatherParameters WeatherPresets::fromState(WeatherState state) {
    switch (state) {
        case WeatherState::Clear: return Clear();
        case WeatherState::Cloudy: return Cloudy();
        case WeatherState::Overcast: return Overcast();
        case WeatherState::Fog: return Fog();
        case WeatherState::LightRain: return LightRain();
        case WeatherState::Rain: return Rain();
        case WeatherState::HeavyRain: return HeavyRain();
        case WeatherState::Thunderstorm: return Thunderstorm();
        case WeatherState::LightSnow: return LightSnow();
        case WeatherState::Snow: return Snow();
        case WeatherState::Blizzard: return Blizzard();
        case WeatherState::Sandstorm: return Sandstorm();
        default: return Clear();
    }
}

// ============================================================================
// ATMOSPHERE
// ============================================================================

void AtmosphereParameters::updateFromTime(float normalizedTime, float latitude) {
    // Calculate sun direction based on time
    float angle = normalizedTime * 2.0f * glm::pi<float>() - glm::pi<float>() * 0.5f;
    
    // Simplified sun position (ignores season)
    float latRad = glm::radians(latitude);
    float elevation = std::sin(angle) * std::cos(latRad);
    float azimuth = std::cos(angle);
    
    sunDirection = glm::normalize(glm::vec3(
        azimuth * std::cos(latRad),
        std::max(elevation, -0.1f),  // Keep slightly below horizon
        std::sin(latRad) * 0.3f
    ));
    
    // Sun intensity based on elevation
    float sunElevation = glm::clamp(sunDirection.y, 0.0f, 1.0f);
    sunIntensity = glm::smoothstep(-0.1f, 0.2f, sunDirection.y);
    
    // Sun color - redder at dawn/dusk
    float sunsetFactor = 1.0f - glm::smoothstep(0.0f, 0.2f, sunDirection.y);
    sunColor = glm::mix(glm::vec3(1.0f, 0.95f, 0.9f), 
                        glm::vec3(1.0f, 0.6f, 0.3f), 
                        sunsetFactor);
    
    // Moon opposite to sun
    moonDirection = -sunDirection;
    moonDirection.y = std::abs(moonDirection.y);
    moonIntensity = (1.0f - sunIntensity) * 0.15f * moonPhase;
    
    // Sky colors
    if (sunDirection.y > 0.1f) {
        // Day
        skyColorZenith = glm::vec3(0.15f, 0.35f, 0.8f);
        skyColorHorizon = glm::vec3(0.5f, 0.7f, 0.95f);
        skyIntensity = 1.0f;
        starIntensity = 0.0f;
    } else if (sunDirection.y > -0.1f) {
        // Twilight
        float t = (sunDirection.y + 0.1f) / 0.2f;
        skyColorZenith = glm::mix(glm::vec3(0.05f, 0.05f, 0.15f), 
                                   glm::vec3(0.15f, 0.35f, 0.8f), t);
        skyColorHorizon = glm::mix(glm::vec3(0.3f, 0.2f, 0.4f), 
                                    glm::vec3(1.0f, 0.6f, 0.4f), 
                                    glm::smoothstep(-0.1f, 0.05f, sunDirection.y));
        skyIntensity = glm::mix(0.1f, 1.0f, t);
        starIntensity = 1.0f - t;
    } else {
        // Night
        skyColorZenith = glm::vec3(0.02f, 0.02f, 0.08f);
        skyColorHorizon = glm::vec3(0.05f, 0.05f, 0.1f);
        skyIntensity = 0.05f;
        starIntensity = 1.0f;
    }
    
    // Ambient light
    ambientColor = glm::mix(skyColorZenith, skyColorHorizon, 0.5f);
    ambientIntensity = glm::max(sunIntensity * 0.3f, 0.05f);
    
    // Star twinkle
    starTwinkle = (starIntensity > 0.0f) ? 0.3f : 0.0f;
}

AtmosphereParameters TimeOfDayLighting::sample(float normalizedTime) const {
    if (keyFrames.empty()) {
        AtmosphereParameters result;
        result.updateFromTime(normalizedTime);
        return result;
    }
    
    if (keyFrames.size() == 1) {
        return keyFrames[0].atmosphere;
    }
    
    // Find surrounding keyframes
    size_t nextIdx = 0;
    for (size_t i = 0; i < keyFrames.size(); ++i) {
        if (keyFrames[i].time > normalizedTime) {
            nextIdx = i;
            break;
        }
    }
    
    size_t prevIdx = (nextIdx == 0) ? keyFrames.size() - 1 : nextIdx - 1;
    
    const auto& prev = keyFrames[prevIdx];
    const auto& next = keyFrames[nextIdx];
    
    // Calculate blend factor
    float range = next.time - prev.time;
    if (range < 0) range += 1.0f;
    
    float pos = normalizedTime - prev.time;
    if (pos < 0) pos += 1.0f;
    
    float t = (range > 0.0001f) ? pos / range : 0.0f;
    
    // Lerp atmosphere
    AtmosphereParameters result;
    result.sunDirection = glm::normalize(glm::mix(prev.atmosphere.sunDirection, 
                                                   next.atmosphere.sunDirection, t));
    result.sunColor = glm::mix(prev.atmosphere.sunColor, next.atmosphere.sunColor, t);
    result.sunIntensity = glm::mix(prev.atmosphere.sunIntensity, next.atmosphere.sunIntensity, t);
    result.moonDirection = glm::normalize(glm::mix(prev.atmosphere.moonDirection, 
                                                    next.atmosphere.moonDirection, t));
    result.moonColor = glm::mix(prev.atmosphere.moonColor, next.atmosphere.moonColor, t);
    result.moonIntensity = glm::mix(prev.atmosphere.moonIntensity, next.atmosphere.moonIntensity, t);
    result.skyColorZenith = glm::mix(prev.atmosphere.skyColorZenith, next.atmosphere.skyColorZenith, t);
    result.skyColorHorizon = glm::mix(prev.atmosphere.skyColorHorizon, next.atmosphere.skyColorHorizon, t);
    result.skyIntensity = glm::mix(prev.atmosphere.skyIntensity, next.atmosphere.skyIntensity, t);
    result.ambientColor = glm::mix(prev.atmosphere.ambientColor, next.atmosphere.ambientColor, t);
    result.ambientIntensity = glm::mix(prev.atmosphere.ambientIntensity, next.atmosphere.ambientIntensity, t);
    result.starIntensity = glm::mix(prev.atmosphere.starIntensity, next.atmosphere.starIntensity, t);
    
    return result;
}

TimeOfDayLighting TimeOfDayLighting::createDefault() {
    TimeOfDayLighting lighting;
    
    // Generate keyframes throughout the day
    for (int hour = 0; hour < 24; hour += 3) {
        KeyFrame kf;
        kf.time = hour / 24.0f;
        kf.atmosphere.updateFromTime(kf.time);
        kf.shadowColor = glm::vec3(0.1f, 0.1f, 0.15f);
        kf.shadowIntensity = 0.5f;
        lighting.keyFrames.push_back(kf);
    }
    
    return lighting;
}

// ============================================================================
// WEATHER ZONE
// ============================================================================

bool WeatherZone::contains(const glm::vec3& position) const {
    glm::vec3 local = position - center;
    return std::abs(local.x) <= extents.x &&
           std::abs(local.y) <= extents.y &&
           std::abs(local.z) <= extents.z;
}

float WeatherZone::getBlendWeight(const glm::vec3& position) const {
    glm::vec3 local = glm::abs(position - center);
    
    // Calculate distance to edge
    glm::vec3 distToEdge = extents - local;
    
    if (distToEdge.x < 0 || distToEdge.y < 0 || distToEdge.z < 0) {
        // Outside zone - check fade distance
        float outsideDist = glm::length(glm::max(-distToEdge, glm::vec3(0)));
        return glm::clamp(1.0f - outsideDist / fadeDistance, 0.0f, 1.0f);
    }
    
    // Inside zone
    float minDist = std::min({distToEdge.x, distToEdge.y, distToEdge.z});
    return glm::clamp(minDist / fadeDistance, 0.0f, 1.0f);
}

// ============================================================================
// WEATHER SYSTEM
// ============================================================================

WeatherSystem::WeatherSystem() {
    todLighting_ = TimeOfDayLighting::createDefault();
    currentParams_ = WeatherPresets::Clear();
    targetParams_ = currentParams_;
    sourceParams_ = currentParams_;
}

void WeatherSystem::init(World& world) {
    // Set default time to noon
    gameTime_.hour = 12;
    gameTime_.minute = 0;
    
    updateAtmosphere();
}

void WeatherSystem::update(World& world, float deltaTime) {
    updateTime(deltaTime);
    updateWeather(deltaTime);
    updateAtmosphere();
    
    // Update weather-affected entities
    for (auto& [entity, component] : world.getComponents<WeatherAffectedComponent>()) {
        // Update wetness
        if (currentParams_.precipitationIntensity > 0.0f && component.affectedByRain) {
            component.currentWetness = glm::min(component.currentWetness + 
                                                 currentParams_.precipitationIntensity * deltaTime * 0.1f, 
                                                 1.0f);
        } else {
            component.currentWetness = glm::max(component.currentWetness - 
                                                 component.wetnessDrySpeed * deltaTime, 
                                                 0.0f);
        }
        
        // Update temperature
        if (component.affectedByTemperature) {
            float targetTemp = currentParams_.temperature;
            component.currentTemperature = glm::mix(component.currentTemperature, 
                                                     targetTemp, 
                                                     component.temperatureChangeSpeed * deltaTime);
        }
    }
}

void WeatherSystem::shutdown(World& world) {
    zones_.clear();
}

void WeatherSystem::updateTime(float deltaTime) {
    if (timePaused_) return;
    
    int oldDay = gameTime_.day;
    int oldHour = gameTime_.hour;
    
    // Advance time
    gameTime_.addSeconds(deltaTime * timeScale_);
    
    // Check for period change
    TimePeriod newPeriod = getTimePeriod();
    if (newPeriod != lastPeriod_) {
        if (onPeriodChanged_) {
            onPeriodChanged_(newPeriod);
        }
        lastPeriod_ = newPeriod;
    }
    
    // Check for day change
    if (gameTime_.day != oldDay && onDayChanged_) {
        onDayChanged_(gameTime_.day);
    }
    
    // Notify time change
    if (onTimeChanged_ && gameTime_.hour != oldHour) {
        onTimeChanged_(gameTime_);
    }
    
    // Update random weather
    if (randomWeatherEnabled_) {
        updateRandomWeather(deltaTime * timeScale_);
    }
}

void WeatherSystem::updateWeather(float deltaTime) {
    // Handle transition
    if (transitionProgress_ < 1.0f && transitionDuration_ > 0.0f) {
        transitionProgress_ += deltaTime / transitionDuration_;
        transitionProgress_ = glm::min(transitionProgress_, 1.0f);
        
        // Smooth step for more natural transitions
        float t = glm::smoothstep(0.0f, 1.0f, transitionProgress_);
        currentParams_ = WeatherParameters::lerp(sourceParams_, targetParams_, t);
        
        if (transitionProgress_ >= 1.0f) {
            currentWeather_ = targetWeather_;
            currentParams_ = targetParams_;
        }
    }
    
    // Update wetness/snow accumulation
    if (currentParams_.precipitationIntensity > 0.0f) {
        if (currentParams_.temperature > 0.0f) {
            // Rain - increase wetness
            wetness_ = glm::min(wetness_ + currentParams_.precipitationIntensity * deltaTime * 0.01f, 1.0f);
            // Snow melts
            snowAccumulation_ = glm::max(snowAccumulation_ - deltaTime * 0.001f, 0.0f);
        } else {
            // Snow - increase accumulation
            snowAccumulation_ = glm::min(snowAccumulation_ + 
                                          currentParams_.precipitationIntensity * deltaTime * 0.005f, 
                                          1.0f);
        }
    } else {
        // Dry out
        wetness_ = glm::max(wetness_ - deltaTime * 0.001f, 0.0f);
        if (currentParams_.temperature > 5.0f) {
            snowAccumulation_ = glm::max(snowAccumulation_ - deltaTime * 0.0001f, 0.0f);
        }
    }
    
    // Lightning
    if (currentParams_.lightningFrequency > 0.0f) {
        lightningTimer_ -= deltaTime;
        if (lightningTimer_ <= 0.0f) {
            triggerLightning();
            // Random interval between strikes
            static std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> dist(5.0f, 30.0f);
            lightningTimer_ = dist(rng) / currentParams_.lightningFrequency;
        }
    }
}

void WeatherSystem::updateAtmosphere() {
    float normalizedTime = gameTime_.getNormalizedTime();
    
    // Sample time of day lighting
    atmosphere_ = todLighting_.sample(normalizedTime);
    
    // Apply weather modifications
    // Cloud coverage affects sun intensity
    float cloudFactor = 1.0f - currentParams_.cloudCoverage * 0.8f;
    atmosphere_.sunIntensity *= cloudFactor;
    
    // Increase ambient during overcast
    atmosphere_.ambientIntensity = glm::mix(atmosphere_.ambientIntensity,
                                             0.4f,
                                             currentParams_.cloudCoverage * 0.5f);
    
    // Fog affects sky visibility
    if (currentParams_.fogDensity > 0.001f) {
        atmosphere_.skyColorZenith = glm::mix(atmosphere_.skyColorZenith,
                                               currentParams_.fogColor,
                                               glm::min(currentParams_.fogDensity * 50.0f, 0.8f));
        atmosphere_.skyColorHorizon = glm::mix(atmosphere_.skyColorHorizon,
                                                currentParams_.fogColor,
                                                glm::min(currentParams_.fogDensity * 100.0f, 0.9f));
    }
}

void WeatherSystem::updateRandomWeather(float gameTimeDelta) {
    weatherChangeTimer_ += gameTimeDelta;
    
    if (weatherChangeTimer_ >= nextWeatherChange_) {
        weatherChangeTimer_ = 0.0f;
        
        // Random next weather based on current
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        
        WeatherState newWeather = currentWeather_;
        float roll = dist(rng);
        
        // Weather transition probabilities
        switch (currentWeather_) {
            case WeatherState::Clear:
                if (roll < 0.3f) newWeather = WeatherState::Cloudy;
                break;
            case WeatherState::Cloudy:
                if (roll < 0.2f) newWeather = WeatherState::Clear;
                else if (roll < 0.4f) newWeather = WeatherState::Overcast;
                else if (roll < 0.5f) newWeather = WeatherState::LightRain;
                break;
            case WeatherState::Overcast:
                if (roll < 0.2f) newWeather = WeatherState::Cloudy;
                else if (roll < 0.5f) newWeather = WeatherState::Rain;
                break;
            case WeatherState::LightRain:
                if (roll < 0.3f) newWeather = WeatherState::Cloudy;
                else if (roll < 0.5f) newWeather = WeatherState::Rain;
                break;
            case WeatherState::Rain:
                if (roll < 0.2f) newWeather = WeatherState::LightRain;
                else if (roll < 0.4f) newWeather = WeatherState::HeavyRain;
                else if (roll < 0.5f) newWeather = WeatherState::Thunderstorm;
                break;
            case WeatherState::HeavyRain:
                if (roll < 0.4f) newWeather = WeatherState::Rain;
                else if (roll < 0.6f) newWeather = WeatherState::Thunderstorm;
                break;
            case WeatherState::Thunderstorm:
                if (roll < 0.5f) newWeather = WeatherState::HeavyRain;
                else if (roll < 0.7f) newWeather = WeatherState::Rain;
                break;
            default:
                // Snow weather - gradually improve
                if (roll < 0.3f) {
                    if (currentWeather_ == WeatherState::Blizzard)
                        newWeather = WeatherState::Snow;
                    else if (currentWeather_ == WeatherState::Snow)
                        newWeather = WeatherState::LightSnow;
                    else
                        newWeather = WeatherState::Cloudy;
                }
                break;
        }
        
        if (newWeather != currentWeather_) {
            transitionToWeather(newWeather, 120.0f);  // 2 minute transition
        }
        
        // Next change in 3-10 game minutes
        std::uniform_real_distribution<float> timeDist(180.0f, 600.0f);
        nextWeatherChange_ = timeDist(rng);
    }
}

void WeatherSystem::triggerLightning() {
    // Could trigger:
    // - Flash screen effect
    // - Thunder sound after delay
    // - Temporary light spike
    // This would typically be handled by rendering/audio systems
}

void WeatherSystem::setTime(const GameTime& time) {
    int oldDay = gameTime_.day;
    gameTime_ = time;
    
    if (gameTime_.day != oldDay && onDayChanged_) {
        onDayChanged_(gameTime_.day);
    }
    
    updateAtmosphere();
}

void WeatherSystem::setTime(int hour, int minute) {
    gameTime_.hour = hour % 24;
    gameTime_.minute = minute % 60;
    gameTime_.second = 0.0f;
    
    updateAtmosphere();
}

TimePeriod WeatherSystem::getTimePeriod() const {
    int hour = gameTime_.hour;
    
    if (hour < 5) return TimePeriod::Night;
    if (hour < 7) return TimePeriod::Dawn;
    if (hour < 11) return TimePeriod::Morning;
    if (hour < 13) return TimePeriod::Noon;
    if (hour < 17) return TimePeriod::Afternoon;
    if (hour < 19) return TimePeriod::Dusk;
    if (hour < 22) return TimePeriod::Evening;
    return TimePeriod::Night;
}

bool WeatherSystem::isNight() const {
    TimePeriod period = getTimePeriod();
    return period == TimePeriod::Night || period == TimePeriod::Evening;
}

void WeatherSystem::setWeather(WeatherState weather) {
    WeatherState oldWeather = currentWeather_;
    currentWeather_ = weather;
    targetWeather_ = weather;
    currentParams_ = WeatherPresets::fromState(weather);
    targetParams_ = currentParams_;
    sourceParams_ = currentParams_;
    transitionProgress_ = 1.0f;
    
    if (oldWeather != weather && onWeatherChanged_) {
        onWeatherChanged_(oldWeather, weather);
    }
}

void WeatherSystem::transitionToWeather(WeatherState weather, float duration) {
    if (weather == targetWeather_) return;
    
    WeatherState oldWeather = currentWeather_;
    targetWeather_ = weather;
    sourceParams_ = currentParams_;
    targetParams_ = WeatherPresets::fromState(weather);
    transitionProgress_ = 0.0f;
    transitionDuration_ = duration;
    
    if (onWeatherChanged_) {
        onWeatherChanged_(oldWeather, weather);
    }
}

void WeatherSystem::setWeatherParameters(const WeatherParameters& params) {
    currentParams_ = params;
    targetParams_ = params;
    sourceParams_ = params;
    transitionProgress_ = 1.0f;
}

void WeatherSystem::registerZone(const WeatherZone& zone) {
    zones_[zone.id] = zone;
}

void WeatherSystem::removeZone(const std::string& id) {
    zones_.erase(id);
}

const WeatherZone* WeatherSystem::getZoneAt(const glm::vec3& position) const {
    const WeatherZone* bestZone = nullptr;
    float bestWeight = 0.0f;
    
    for (const auto& [id, zone] : zones_) {
        float weight = zone.getBlendWeight(position);
        if (weight > bestWeight) {
            bestWeight = weight;
            bestZone = &zone;
        }
    }
    
    return (bestWeight > 0.0f) ? bestZone : nullptr;
}

glm::vec3 WeatherSystem::getWindAt(const glm::vec3& position) const {
    glm::vec3 wind = currentParams_.windDirection * currentParams_.windSpeed;
    
    // Add gusts
    if (currentParams_.windGustStrength > 0.0f) {
        float gustPhase = currentParams_.windGustFrequency * 
                          static_cast<float>(gameTime_.second + gameTime_.minute * 60);
        float gust = std::sin(gustPhase) * 0.5f + 0.5f;
        wind *= (1.0f + gust * currentParams_.windGustStrength);
    }
    
    // Apply zone modifiers
    const WeatherZone* zone = getZoneAt(position);
    if (zone) {
        wind *= zone->windMultiplier;
    }
    
    return wind;
}

bool WeatherSystem::isSheltered(const glm::vec3& position) const {
    // Would typically do a raycast up to check for cover
    // For now, return false (always exposed)
    return false;
}

float WeatherSystem::getWetness(const glm::vec3& position) const {
    if (isSheltered(position)) {
        return 0.0f;  // No wetness under cover
    }
    return wetness_;
}

float WeatherSystem::getSnowAccumulation(const glm::vec3& position) const {
    if (isSheltered(position)) {
        return 0.0f;  // No snow under cover
    }
    return snowAccumulation_;
}

} // namespace Sanic
