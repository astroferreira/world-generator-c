#pragma once

#include "terrain/TerrainData.hpp"
#include "NoiseGenerator.hpp"
#include <vector>
#include <functional>

namespace worldgen {

struct ContinentGenConfig {
    float targetLandRatio = 0.3f;           // Earth-like: 30% land
    float continentFrequency = 0.0008f;     // Low freq for large continents
    int continentOctaves = 4;
    float continentPersistence = 0.45f;
    float coastlineWarpAmp = 120.0f;        // Strong warping for organic coasts
    float coastlineWarpFrequency = 0.001f;
    int coastlineWarpOctaves = 3;
    float transitionWidth = 0.08f;          // Smooth coastline transition
};

struct TerrainGenConfig {
    size_t width = 2048;
    size_t height = 2048;
    int seed = 42;

    struct NoiseLayer {
        NoiseConfig noise;
        float weight = 1.0f;
    };
    std::vector<NoiseLayer> layers;

    // Legacy single-island mode
    bool applyIslandMask = false;
    float islandFalloff = 0.4f;

    // Multi-continent generation
    bool applyMultiContinent = true;
    ContinentGenConfig continentConfig;
};

class TerrainGenerator {
public:
    using ProgressCallback = std::function<void(float progress, const char* stage)>;

    TerrainGenerator();
    explicit TerrainGenerator(const TerrainGenConfig& config);

    void configure(const TerrainGenConfig& config);
    TerrainData generate();
    TerrainData generate(ProgressCallback callback);

private:
    TerrainGenConfig m_config;
    std::vector<NoiseGenerator> m_noiseGenerators;

    void setupDefaultLayers();
    void blendLayers(Heightmap& heightmap);
    void applyIslandMask(Heightmap& heightmap);

    // Multi-continent generation
    void generateContinentalMask(Heightmap& mask);
    float calculateSeaLevel(const Heightmap& mask, float targetLandRatio);
    void applyContinentalMask(Heightmap& heightmap, const Heightmap& mask, float seaLevel);
};

} // namespace worldgen
