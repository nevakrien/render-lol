#pragma once

namespace toy::tuning {
// Collision freshness and scoring.
inline constexpr float fullImpactSpeed = 8.0f;
inline constexpr float fullImpactPointBonus = 10.0f;
inline constexpr float pairFreshnessDecay = .62f;
inline constexpr float pairFreshnessRecoverySeconds = 1.4f;
inline constexpr float visibleImpactThreshold = .01f;
inline constexpr float impactLifetimeSeconds = .6f;
inline constexpr float impactBaseOpacity = .8f;

// Heat is an exponential moving average of the collision load seen each tick.
inline constexpr float heatSmoothingSeconds = 1.35f;
inline constexpr float heatPeak = .65f;
inline constexpr float heatPowerDeclineEnd = 1.15f;
inline constexpr float heatBurnout = 1.55f;
inline constexpr float heatMaximum = 2.0f;
inline constexpr float heatPeakPower = 2.0f;
inline constexpr float heatPowerAtDeclineEnd = .65f;
inline constexpr float heatPowerDeclineRange = heatPowerDeclineEnd - heatPeak;
inline constexpr float heatBurnoutRange = heatBurnout - heatPowerDeclineEnd;
inline constexpr float heatVisualDamageRange = heatBurnout - heatPeak;

inline float heatPower(float heat) {
    if (heat <= heatPeak)
        return 1.0f + (heatPeakPower - 1.0f) * heat / heatPeak;
    if (heat <= heatPowerDeclineEnd) {
        float decline = (heat - heatPeak) / heatPowerDeclineRange;
        return heatPeakPower - decline * (heatPeakPower - heatPowerAtDeclineEnd);
    }
    float burnout = (heat - heatPowerDeclineEnd) / heatBurnoutRange;
    float power = heatPowerAtDeclineEnd * (1.0f - burnout);
    return power > 0.0f ? power : 0.0f;
}

inline float normalizedHeatPower(float heat) { return heatPower(heat) / heatPeakPower; }

inline constexpr float collisionLoadBase = .55f;
inline constexpr float collisionLoadFromStrength = .45f;
inline constexpr float collisionLoadFromStaleness = .35f;

// Wall routing rewards fresh movement around the arena.
inline constexpr float routeFreshnessThreshold = .35f;
inline constexpr float routeWindowSeconds = 2.5f;
inline constexpr float adjacentWallMultiplier = 1.2f;

// Physical response follows the same rise-and-fall shape as impact power.
inline constexpr float peakRestitution = .99f;
inline constexpr float burnoutRestitution = .15f;

// Body heat presentation.
inline constexpr float heatFireAppearance = .12f;
inline constexpr float heatSurfaceOpacity = .74f;
inline constexpr float burnedSurfaceOpacity = .24f;
inline constexpr int minimumFireSpots = 10;
inline constexpr int additionalFireSpots = 12;
inline constexpr float minimumFireSpotRadius = .035f;
inline constexpr float additionalFireSpotRadius = .09f;
inline constexpr float maximumShake = .055f;
inline constexpr float damageAppearanceThreshold = .03f;
inline constexpr float smokeAppearanceThreshold = .38f;
inline constexpr int minimumDamageSpots = 8;
inline constexpr int additionalDamageSpots = 11;
inline constexpr int minimumCracks = 2;
inline constexpr int additionalCracks = 6;

// Collision audio follows output power but fades further near burnout.
inline constexpr float minimumAudibleImpact = .04f;
inline constexpr float audioFadeHeat = .8f;
inline constexpr float baseDetune = .04f;
inline constexpr float heatDetune = .14f;
inline constexpr float maximumPitchDrop = .08f;
inline constexpr float baseImpactFrequency = 320.0f;
inline constexpr float impactPitchStep = 70.0f;
inline constexpr float baseImpactAmplitude = .05f;
inline constexpr float impactAmplitudeScale = .17f;
inline constexpr float maximumAudioStrength = 1.5f;
} // namespace toy::tuning
