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
inline constexpr int maximumSimultaneousImpactVoices = 1024;
inline constexpr float minimumAudibleImpact = .04f;
inline constexpr float audioFadeHeat = .4f;
inline constexpr float minimumImpactFrequency = 300.0f;
inline constexpr float maximumImpactFrequency = 453.0f;
inline constexpr float heatPitchWeight = .90f;//0.0f;//.41f*0.03f;
inline constexpr float impactSpeedPitchWeight = .10f;//0.0f;//.59f*0.03f;
inline constexpr float referenceImpactMass = 2.0f;
inline constexpr float massPitchExponent = .35f;
inline constexpr float baseImpactAmplitude = .15f;
inline constexpr float impactAmplitudeScale = .05f;
inline constexpr float maximumAudioStrength = 1.5f;
inline constexpr float maximumAWeightedImpactDecibels = -24.0f;
inline constexpr float impactAWeightingAmount = 1.0f;

// Impact texture moves continuously from a tonal boop to a short, noisy click.
inline constexpr float impactDurationSeconds = .1f;
inline constexpr float slowImpactAttackSeconds = .003f;
inline constexpr float fastImpactAttackSeconds = .0005f;
inline constexpr float impactReleaseSeconds = .005f;
inline constexpr float impactTextureExponent = 1.2f;
inline constexpr float baseToneDecayRate = 55.0f;
inline constexpr float speedToneDecayRate = 80.0f;

// Per-impact variation is the full random range around the configured value.
inline constexpr float basePitchVariation = .04f;
inline constexpr float texturePitchVariation = .08f;

// Each mode is a resonance at base pitch times this ratio. Values just above
// 2x and 3x avoid perfectly aligned harmonics when many impacts overlap.
inline constexpr float secondModeFrequencyRatio = 1.30f;
inline constexpr float thirdModeFrequencyRatio = 1.45f;
inline constexpr float secondModeRatioVariation = .08f;
inline constexpr float thirdModeRatioVariation = .12f;
inline constexpr float fastSecondModeAmount = .12f;
inline constexpr float fastThirdModeAmount = .07f;

inline constexpr float fastNoiseAmount = .19f;
inline constexpr float noiseAttackSeconds = .0002f;
inline constexpr float noiseDecayRate = 600.0f;
inline constexpr float limiterThreshold = .12f;
} // namespace toy::tuning
