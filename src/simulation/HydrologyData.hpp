#pragma once

#include "terrain/Heightmap.hpp"
#include <memory>
#include <cstdint>
#include <vector>

namespace worldgen {

// Persistent spring location for spring network
struct SpringLocation {
    size_t x, y;
    float flowRate;   // Base flow rate (modified by season)
    float quality;    // Terrain suitability score (0-1)
};

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

    // Rainfall - increased for realistic precipitation
    float baseRainfall = 0.05f;              // Increased: base rainfall (was 0.01)
    float elevationRainfallBonus = 0.08f;    // Increased: orographic effect (was 0.02)

    // Springs - increased for sustained river sources
    float springElevationMin = 0.35f;        // Lower minimum (was 0.4)
    float springElevationMax = 0.75f;        // Higher maximum (was 0.7)
    float springFlowRate = 0.2f;             // Increased: stronger springs (was 0.05)
    float springDensity = 0.003f;            // Increased: more springs (was 0.0005)

    // Spring network - terrain-aware spring placement
    float springCurvatureWeight = 0.4f;      // Prefer valleys (concave terrain)
    float springFlowAccumWeight = 0.3f;      // Prefer convergent flow areas
    float springSlopeWeight = 0.2f;          // Prefer gentle slopes
    float springElevationWeight = 0.1f;      // Elevation band preference
    int maxSprings = 500;                    // Maximum number of springs
    float springMinSpacing = 10.0f;          // Minimum pixels between springs

    // Wind and rain shadow
    float windDirection = 270.0f;            // Azimuth in degrees (270 = from west)
    float rainShadowStrength = 0.6f;         // Max rainfall reduction behind mountains (0-1)
    float rainShadowDistance = 50.0f;        // How far upwind to sample (pixels)
    float moistureDepletionRate = 0.02f;     // Moisture lost per unit elevation climbed

    // Water particle lifetime - water is lost through erosion and evaporation
    float waterLifetimeBase = 100.0f;        // Base lifetime in simulation steps
    float evaporationRate = 0.02f;           // Water lost per step due to evaporation
    float erosionWaterLoss = 0.1f;           // Water consumed per unit of erosion
    float temperatureEvapBonus = 0.5f;       // Extra evap at low elevation (warmer)

    // River dynamics
    float channelErosionRate = 0.002f;       // Increased: faster channel carving (was 0.0005)
    float sedimentDepositionRate = 0.001f;
    float meanderStrength = 0.05f;
    float deltaFormationRate = 0.003f;

    // Channel initialization
    float initialChannelFactor = 0.3f;       // Fraction of flow-based channel to create immediately

    // Continuous mode - recalculate flow network as terrain changes
    int recalculateInterval = 5;  // Recalculate full flow network every N steps
    bool continuous = true;       // Keep running alongside other simulations

    // Completion (only used if continuous = false)
    int maxIterations = 50;
    float convergenceThreshold = 0.0001f;

    // Stream order (Strahler) - for realistic drainage hierarchy
    float firstOrderThreshold = 0.0005f;   // Min flow fraction for order-1 stream
    int maxStreamOrder = 7;                // Cap stream order display
    // Width scaling by stream order (order 0-7)
    float orderWidthScale[8] = {0.3f, 0.5f, 0.8f, 1.2f, 2.0f, 3.5f, 5.0f, 8.0f};

    // Rivulets - fine detail streams from rainfall
    float rivuletThreshold = 0.0001f;      // Very low threshold for tiny streams
    float rivuletDepthScale = 0.002f;      // Base depth for rivulets
    bool showRivulets = true;              // Enable fine detail streams

    // Stream power erosion - valley carving
    float streamPowerErosionCoeff = 0.001f;    // Erosion rate coefficient
    float maxChannelErosionPerStep = 0.005f;   // Max erosion per step
    float lateralErosionFactor = 0.3f;         // Lateral erosion for valley widening

    // Sediment transport - capacity-based
    float sedimentCapacityCoeff = 0.5f;    // Transport capacity coefficient
    float sedimentErodeRate = 0.1f;        // Rate of sediment pickup
    float sedimentDepositRate = 0.2f;      // Rate of sediment deposition

    // Spring channel visibility
    float springChannelDepth = 0.002f;     // Channel depth from springs
    float springWaterVisibility = 0.1f;    // Water visibility at springs
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

    // Water particle system - tracks water amount and age for lifetime/evaporation
    std::unique_ptr<Heightmap> waterAmount;   // Current water volume at each cell
    std::unique_ptr<Heightmap> waterAge;      // Age of water (for lifetime decay)

    // Rain shadow - moisture availability factor (0-1)
    std::unique_ptr<Heightmap> moistureFactor;

    // Stream order (Strahler) - for realistic drainage hierarchy
    std::unique_ptr<Heightmap> streamOrder;   // Strahler order (1-7+)
    std::unique_ptr<Heightmap> streamPower;   // Stream power index for erosion

    // Spring network - persistent spring locations
    std::vector<SpringLocation> springs;
    bool springsInitialized = false;

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
        state.waterAmount = std::make_unique<Heightmap>(width, height);
        state.waterAge = std::make_unique<Heightmap>(width, height);
        state.moistureFactor = std::make_unique<Heightmap>(width, height);
        state.streamOrder = std::make_unique<Heightmap>(width, height);
        state.streamPower = std::make_unique<Heightmap>(width, height);
        return state;
    }
};

} // namespace worldgen
