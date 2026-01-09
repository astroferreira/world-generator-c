#pragma once

#include "terrain/Heightmap.hpp"
#include <memory>
#include <cstdint>

namespace worldgen {

// Flow direction encoding (D8 algorithm)
enum class FlowDirection : uint8_t {
    None      = 0,
    North     = 1 << 0,
    NorthEast = 1 << 1,
    East      = 1 << 2,
    SouthEast = 1 << 3,
    South     = 1 << 4,
    SouthWest = 1 << 5,
    West      = 1 << 6,
    NorthWest = 1 << 7
};

inline FlowDirection operator|(FlowDirection a, FlowDirection b) {
    return static_cast<FlowDirection>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool hasDirection(FlowDirection flags, FlowDirection dir) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(dir)) != 0;
}

// Direction offsets lookup table
struct DirectionInfo {
    int dx, dy;
    float distance;
};

constexpr DirectionInfo DIRECTION_OFFSETS[8] = {
    { 0, -1, 1.0f},      // North
    { 1, -1, 1.414f},    // NorthEast
    { 1,  0, 1.0f},      // East
    { 1,  1, 1.414f},    // SouthEast
    { 0,  1, 1.0f},      // South
    {-1,  1, 1.414f},    // SouthWest
    {-1,  0, 1.0f},      // West
    {-1, -1, 1.414f}     // NorthWest
};

// Seasonal state
enum class Season : uint8_t {
    Spring = 0,
    Summer = 1,
    Autumn = 2,
    Winter = 3
};

// Configuration for hydrology simulation
struct HydrologyParams {
    // Flow calculation
    float minFlowThreshold = 0.001f;
    float flowExponent = 1.1f;

    // Lake formation
    float lakeEvaporationRate = 0.0001f;
    float maxLakeDepth = 0.3f;

    // River properties
    float riverThresholdRatio = 0.002f;      // 0.2% of max flow = river (very low for fine tributaries)
    float baseRiverDepth = 0.05f;            // Base depth for small streams
    float riverDepthScale = 0.5f;            // Depth scaling with flow
    float maxRiverWidth = 10.0f;             // Max river width in pixels for largest rivers
    float seaLevel = 0.32f;                  // Only show rivers above this elevation

    // Seasonal variation
    float springMultiplier = 1.5f;
    float summerMultiplier = 0.6f;
    float autumnMultiplier = 1.0f;
    float winterMultiplier = 0.3f;

    // Rainfall
    float baseRainfall = 0.01f;
    float elevationRainfallBonus = 0.02f;

    // Springs
    float springElevationMin = 0.4f;
    float springElevationMax = 0.7f;
    float springFlowRate = 0.05f;
    float springDensity = 0.0005f;

    // River dynamics
    float channelErosionRate = 0.0005f;
    float sedimentDepositionRate = 0.001f;
    float meanderStrength = 0.05f;
    float deltaFormationRate = 0.003f;

    // Completion
    int maxIterations = 50;
    float convergenceThreshold = 0.0001f;
};

// Configuration for glacier system
struct GlacierParams {
    // Elevation thresholds (normalized 0-1)
    float snowlineElevation = 0.90f;         // Higher = less snow
    float glacierElevation = 0.95f;          // Higher = less glaciers

    // Accumulation/ablation
    float snowAccumulationRate = 0.005f;
    float iceCompactionRate = 0.001f;
    float meltRatePerDegree = 0.003f;
    float sublimationRate = 0.0001f;

    // Glacier flow
    bool enableGlacierFlow = true;
    float flowViscosity = 0.0001f;
    float erosionStrength = 0.0005f;

    // Seasonal
    float winterSnowMultiplier = 2.5f;
    float summerMeltMultiplier = 2.0f;

    // Visual caps
    float maxIceThickness = 0.15f;
    float maxSnowDepth = 0.04f;

    // Completion
    int maxIterations = 30;
};

// Extended terrain data for hydrology
struct HydrologyState {
    std::unique_ptr<Heightmap> flowAccumulation;
    std::unique_ptr<Heightmap> flowDirection;
    std::unique_ptr<Heightmap> lakeDepth;
    std::unique_ptr<Heightmap> riverChannel;
    std::unique_ptr<Heightmap> iceThickness;
    std::unique_ptr<Heightmap> snowpack;
    std::unique_ptr<Heightmap> meltwater;

    Season currentSeason = Season::Spring;
    float seasonProgress = 0.0f;
    int yearCount = 0;

    static HydrologyState create(size_t width, size_t height) {
        HydrologyState state;
        state.flowAccumulation = std::make_unique<Heightmap>(width, height);
        state.flowDirection = std::make_unique<Heightmap>(width, height);
        state.lakeDepth = std::make_unique<Heightmap>(width, height);
        state.riverChannel = std::make_unique<Heightmap>(width, height);
        state.iceThickness = std::make_unique<Heightmap>(width, height);
        state.snowpack = std::make_unique<Heightmap>(width, height);
        state.meltwater = std::make_unique<Heightmap>(width, height);
        return state;
    }
};

} // namespace worldgen
