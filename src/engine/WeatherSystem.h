/**
 * WeatherSystem.h
 * 
 * Dynamic Weather and Time of Day System
 * 
 * Features:
 * - Day/night cycle with proper lighting
 * - Weather states (clear, rain, snow, fog, storm)
 * - Seamless weather transitions
 * - Weather effects on gameplay
 * - Procedural cloud movement
 * - Atmospheric scattering
 * 
 * Reference:
 *   Engine/Source/Runtime/Engine/Private/Atmosphere/
 */

#pragma once

#include "ECS.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Sanic {

// Forward declarations
class WeatherSystem;

// ============================================================================
// TIME OF DAY
// ============================================================================

/**
 * Time representation
 */
struct GameTime {
    int day = 1;
    int hour = 12;
    int minute = 0;
    float second = 0.0f;
    
    /**
     * Get time as fraction of day (0.0 = midnight, 0.5 = noon)
     */
    float getNormalizedTime() const {
        return (hour + minute / 60.0f + second / 3600.0f) / 24.0f;
    }
    
    /**
     * Set from normalized time
     */
    void setFromNormalized(float t) {
        t = t - static_cast<int>(t);  // Wrap to 0-1
        float totalHours = t * 24.0f;
        hour = static_cast<int>(totalHours);
        float remainingMinutes = (totalHours - hour) * 60.0f;
        minute = static_cast<int>(remainingMinutes);
        second = (remainingMinutes - minute) * 60.0f;
    }
    
    /**
     * Add seconds
     */
    void addSeconds(float seconds) {
        this->second += seconds;
        while (this->second >= 60.0f) {
            this->second -= 60.0f;
            minute++;
        }
        while (minute >= 60) {
            minute -= 60;
            hour++;
        }
        while (hour >= 24) {
            hour -= 24;
            day++;
        }
    }
    
    /**
     * Get formatted time string
     */
    std::string toString() const {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Day %d, %02d:%02d", day, hour, minute);
        return buffer;
    }
};

/**
 * Time period
 */
enum class TimePeriod {
    Night,      // 00:00 - 05:00
    Dawn,       // 05:00 - 07:00
    Morning,    // 07:00 - 11:00
    Noon,       // 11:00 - 13:00
    Afternoon,  // 13:00 - 17:00
    Dusk,       // 17:00 - 19:00
    Evening     // 19:00 - 24:00
};

// ============================================================================
// WEATHER TYPES
// ============================================================================

/**
 * Weather states
 */
enum class WeatherState {
    Clear,
    Cloudy,
    Overcast,
    Fog,
    LightRain,
    Rain,
    HeavyRain,
    Thunderstorm,
    LightSnow,
    Snow,
    Blizzard,
    Sandstorm
};

/**
 * Weather parameters
 */
struct WeatherParameters {
    WeatherState state = WeatherState::Clear;
    
    // Sky
    float cloudCoverage = 0.0f;       // 0 = clear, 1 = overcast
    float cloudDensity = 0.5f;
    float cloudSpeed = 0.1f;
    glm::vec3 cloudColor = glm::vec3(1.0f);
    
    // Fog
    float fogDensity = 0.0f;
    float fogHeight = 100.0f;
    glm::vec3 fogColor = glm::vec3(0.7f, 0.8f, 0.9f);
    
    // Precipitation
    float precipitationIntensity = 0.0f;  // 0 = none, 1 = heavy
    float precipitationSize = 1.0f;
    glm::vec3 precipitationColor = glm::vec3(0.8f, 0.8f, 0.9f);
    
    // Wind
    glm::vec3 windDirection = glm::vec3(1, 0, 0);
    float windSpeed = 0.0f;
    float windGustStrength = 0.0f;
    float windGustFrequency = 0.0f;
    
    // Lightning
    float lightningFrequency = 0.0f;
    float lightningIntensity = 0.0f;
    
    // Temperature (affects gameplay)
    float temperature = 20.0f;  // Celsius
    float humidity = 0.5f;
    
    // Visibility
    float visibility = 10000.0f;  // Meters
    
    // Audio
    float ambientVolume = 1.0f;
    std::string ambientSound;
    
    /**
     * Lerp between two weather states
     */
    static WeatherParameters lerp(const WeatherParameters& a, 
                                   const WeatherParameters& b, 
                                   float t);
};

/**
 * Predefined weather presets
 */
struct WeatherPresets {
    static WeatherParameters Clear();
    static WeatherParameters Cloudy();
    static WeatherParameters Overcast();
    static WeatherParameters Fog();
    static WeatherParameters LightRain();
    static WeatherParameters Rain();
    static WeatherParameters HeavyRain();
    static WeatherParameters Thunderstorm();
    static WeatherParameters LightSnow();
    static WeatherParameters Snow();
    static WeatherParameters Blizzard();
    static WeatherParameters Sandstorm();
    
    static WeatherParameters fromState(WeatherState state);
};

// ============================================================================
// LIGHTING
// ============================================================================

/**
 * Sky/atmosphere parameters
 */
struct AtmosphereParameters {
    // Sun
    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.9f);
    float sunIntensity = 1.0f;
    float sunDiskSize = 0.01f;
    
    // Moon
    glm::vec3 moonDirection = glm::normalize(glm::vec3(-0.5f, 0.5f, -0.3f));
    glm::vec3 moonColor = glm::vec3(0.5f, 0.55f, 0.6f);
    float moonIntensity = 0.1f;
    float moonPhase = 0.0f;  // 0 = new, 0.5 = full
    
    // Sky
    glm::vec3 skyColorZenith = glm::vec3(0.2f, 0.4f, 0.8f);
    glm::vec3 skyColorHorizon = glm::vec3(0.6f, 0.7f, 0.9f);
    float skyIntensity = 1.0f;
    
    // Ambient
    glm::vec3 ambientColor = glm::vec3(0.3f, 0.35f, 0.4f);
    float ambientIntensity = 0.3f;
    
    // Stars
    float starIntensity = 0.0f;
    float starTwinkle = 0.0f;
    
    // Rayleigh/Mie scattering
    float rayleighScale = 1.0f;
    float mieScale = 1.0f;
    float mieG = 0.76f;
    
    /**
     * Calculate sun position from time
     */
    void updateFromTime(float normalizedTime, float latitude = 45.0f);
};

/**
 * Lighting parameters for time of day
 */
struct TimeOfDayLighting {
    // Key times and their lighting
    struct KeyFrame {
        float time;  // 0-1 normalized
        AtmosphereParameters atmosphere;
        glm::vec3 shadowColor;
        float shadowIntensity;
    };
    
    std::vector<KeyFrame> keyFrames;
    
    /**
     * Sample lighting at time
     */
    AtmosphereParameters sample(float normalizedTime) const;
    
    /**
     * Create default day/night cycle
     */
    static TimeOfDayLighting createDefault();
};

// ============================================================================
// WEATHER ZONE
// ============================================================================

/**
 * A region with specific weather
 */
struct WeatherZone {
    std::string id;
    std::string name;
    
    // Bounds
    glm::vec3 center = glm::vec3(0);
    glm::vec3 extents = glm::vec3(1000);
    float fadeDistance = 100.0f;  // Transition distance at edges
    
    // Weather
    WeatherState preferredWeather = WeatherState::Clear;
    std::vector<std::pair<WeatherState, float>> possibleWeather;  // state, weight
    
    // Modifiers
    float temperatureOffset = 0.0f;
    float humidityOffset = 0.0f;
    float windMultiplier = 1.0f;
    
    /**
     * Check if position is inside zone
     */
    bool contains(const glm::vec3& position) const;
    
    /**
     * Get blend weight for position (0 = outside, 1 = fully inside)
     */
    float getBlendWeight(const glm::vec3& position) const;
};

// ============================================================================
// WEATHER SYSTEM
// ============================================================================

/**
 * Main weather and time of day system
 */
class WeatherSystem : public System {
public:
    WeatherSystem();
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    // ================== TIME ==================
    
    /**
     * Get current game time
     */
    const GameTime& getTime() const { return gameTime_; }
    
    /**
     * Set game time
     */
    void setTime(const GameTime& time);
    void setTime(int hour, int minute);
    
    /**
     * Get time speed multiplier
     */
    float getTimeScale() const { return timeScale_; }
    
    /**
     * Set time speed (1.0 = real time, 60.0 = 1 hour per minute)
     */
    void setTimeScale(float scale) { timeScale_ = scale; }
    
    /**
     * Pause/resume time
     */
    void setTimePaused(bool paused) { timePaused_ = paused; }
    bool isTimePaused() const { return timePaused_; }
    
    /**
     * Get current time period
     */
    TimePeriod getTimePeriod() const;
    
    /**
     * Check if it's night
     */
    bool isNight() const;
    
    // ================== WEATHER ==================
    
    /**
     * Get current weather
     */
    WeatherState getCurrentWeather() const { return currentWeather_; }
    
    /**
     * Get current weather parameters
     */
    const WeatherParameters& getWeatherParameters() const { return currentParams_; }
    
    /**
     * Set weather immediately
     */
    void setWeather(WeatherState weather);
    
    /**
     * Transition to weather over time
     */
    void transitionToWeather(WeatherState weather, float duration = 60.0f);
    
    /**
     * Set custom weather parameters
     */
    void setWeatherParameters(const WeatherParameters& params);
    
    /**
     * Enable/disable random weather changes
     */
    void setRandomWeatherEnabled(bool enabled) { randomWeatherEnabled_ = enabled; }
    bool isRandomWeatherEnabled() const { return randomWeatherEnabled_; }
    
    // ================== ATMOSPHERE ==================
    
    /**
     * Get atmosphere parameters
     */
    const AtmosphereParameters& getAtmosphere() const { return atmosphere_; }
    
    /**
     * Set time of day lighting
     */
    void setTimeOfDayLighting(const TimeOfDayLighting& lighting) { 
        todLighting_ = lighting; 
    }
    
    // ================== ZONES ==================
    
    /**
     * Register weather zone
     */
    void registerZone(const WeatherZone& zone);
    
    /**
     * Remove weather zone
     */
    void removeZone(const std::string& id);
    
    /**
     * Get zone at position
     */
    const WeatherZone* getZoneAt(const glm::vec3& position) const;
    
    // ================== EFFECTS ==================
    
    /**
     * Get current wind at position
     */
    glm::vec3 getWindAt(const glm::vec3& position) const;
    
    /**
     * Check if position is sheltered from rain
     */
    bool isSheltered(const glm::vec3& position) const;
    
    /**
     * Get wetness at position (for puddles, wet surfaces)
     */
    float getWetness(const glm::vec3& position) const;
    
    /**
     * Get snow accumulation at position
     */
    float getSnowAccumulation(const glm::vec3& position) const;
    
    // ================== CALLBACKS ==================
    
    using WeatherCallback = std::function<void(WeatherState, WeatherState)>;
    using TimeCallback = std::function<void(const GameTime&)>;
    
    void setOnWeatherChanged(WeatherCallback callback) { onWeatherChanged_ = callback; }
    void setOnTimeChanged(TimeCallback callback) { onTimeChanged_ = callback; }
    void setOnDayChanged(std::function<void(int)> callback) { onDayChanged_ = callback; }
    void setOnPeriodChanged(std::function<void(TimePeriod)> callback) { onPeriodChanged_ = callback; }
    
private:
    void updateTime(float deltaTime);
    void updateWeather(float deltaTime);
    void updateAtmosphere();
    void updateRandomWeather(float deltaTime);
    void triggerLightning();
    
    // Time
    GameTime gameTime_;
    float timeScale_ = 60.0f;  // 1 game hour = 1 real minute
    bool timePaused_ = false;
    TimePeriod lastPeriod_ = TimePeriod::Noon;
    
    // Weather
    WeatherState currentWeather_ = WeatherState::Clear;
    WeatherState targetWeather_ = WeatherState::Clear;
    WeatherParameters currentParams_;
    WeatherParameters sourceParams_;
    WeatherParameters targetParams_;
    float transitionProgress_ = 1.0f;
    float transitionDuration_ = 0.0f;
    
    bool randomWeatherEnabled_ = true;
    float weatherChangeTimer_ = 0.0f;
    float nextWeatherChange_ = 300.0f;  // 5 minutes default
    
    // Atmosphere
    AtmosphereParameters atmosphere_;
    TimeOfDayLighting todLighting_;
    
    // Zones
    std::unordered_map<std::string, WeatherZone> zones_;
    
    // Effects
    float wetness_ = 0.0f;
    float snowAccumulation_ = 0.0f;
    float lightningTimer_ = 0.0f;
    
    // Callbacks
    WeatherCallback onWeatherChanged_;
    TimeCallback onTimeChanged_;
    std::function<void(int)> onDayChanged_;
    std::function<void(TimePeriod)> onPeriodChanged_;
};

// ============================================================================
// WEATHER COMPONENT
// ============================================================================

/**
 * Component for entities affected by weather
 */
struct WeatherAffectedComponent {
    bool affectedByRain = true;
    bool affectedByWind = true;
    bool affectedByTemperature = true;
    
    // Current states
    float currentWetness = 0.0f;
    float currentTemperature = 20.0f;
    
    // Thresholds
    float wetnessDrySpeed = 0.1f;
    float temperatureChangeSpeed = 0.5f;
};

} // namespace Sanic
