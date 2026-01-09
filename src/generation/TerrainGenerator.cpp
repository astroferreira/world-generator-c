#include "generation/TerrainGenerator.hpp"
#include "utils/Math.hpp"
#include <cmath>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

TerrainGenerator::TerrainGenerator() {
    setupDefaultLayers();
}

TerrainGenerator::TerrainGenerator(const TerrainGenConfig& config)
    : m_config(config)
{
    if (m_config.layers.empty()) {
        setupDefaultLayers();
    }
}

void TerrainGenerator::configure(const TerrainGenConfig& config) {
    m_config = config;
    if (m_config.layers.empty()) {
        setupDefaultLayers();
    }
}

void TerrainGenerator::setupDefaultLayers() {
    m_config.layers.clear();

    // Base continental layer - large features
    NoiseConfig continentNoise;
    continentNoise.noiseType = NoiseConfig::Type::Simplex;
    continentNoise.fractalType = NoiseConfig::FractalType::FBm;
    continentNoise.frequency = 0.002f;
    continentNoise.octaves = 6;
    continentNoise.persistence = 0.5f;
    continentNoise.domainWarpAmp = 50.0f;
    m_config.layers.push_back({continentNoise, 1.0f});

    // Mountain ridges
    NoiseConfig ridgeNoise;
    ridgeNoise.noiseType = NoiseConfig::Type::Simplex;
    ridgeNoise.fractalType = NoiseConfig::FractalType::Ridged;
    ridgeNoise.frequency = 0.005f;
    ridgeNoise.octaves = 5;
    ridgeNoise.persistence = 0.5f;
    m_config.layers.push_back({ridgeNoise, 0.4f});

    // Detail layer
    NoiseConfig detailNoise;
    detailNoise.noiseType = NoiseConfig::Type::Simplex;
    detailNoise.fractalType = NoiseConfig::FractalType::FBm;
    detailNoise.frequency = 0.02f;
    detailNoise.octaves = 4;
    detailNoise.persistence = 0.4f;
    m_config.layers.push_back({detailNoise, 0.15f});
}

TerrainData TerrainGenerator::generate() {
    return generate(nullptr);
}

TerrainData TerrainGenerator::generate(ProgressCallback callback) {
    if (callback) callback(0.0f, "Creating terrain data");

    TerrainData terrain = TerrainData::create(m_config.width, m_config.height);

    // Setup noise generators
    m_noiseGenerators.clear();
    for (auto& layer : m_config.layers) {
        layer.noise.seed = m_config.seed;
        m_noiseGenerators.emplace_back(layer.noise);
    }

    if (callback) callback(0.2f, "Generating noise layers");

    // Blend all layers
    blendLayers(*terrain.height);

    if (callback) callback(0.5f, "Generating land masses");

    // Apply land mass generation
    if (m_config.applyMultiContinent) {
        Heightmap continentMask(m_config.width, m_config.height);

        if (callback) callback(0.55f, "Generating continental mask");
        generateContinentalMask(continentMask);

        if (callback) callback(0.65f, "Calculating sea level");
        float seaLevel = calculateSeaLevel(continentMask, m_config.continentConfig.targetLandRatio);

        if (callback) callback(0.75f, "Applying continental mask");
        applyContinentalMask(*terrain.height, continentMask, seaLevel);
    }
    // Legacy single-island mode
    else if (m_config.applyIslandMask) {
        if (callback) callback(0.7f, "Applying island mask");
        applyIslandMask(*terrain.height);
    }

    if (callback) callback(0.9f, "Normalizing");

    // Normalize to [0, 1]
    terrain.height->normalize();

    if (callback) callback(1.0f, "Done");

    return terrain;
}

void TerrainGenerator::blendLayers(Heightmap& heightmap) {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float value = 0.0f;
            float totalWeight = 0.0f;

            for (size_t i = 0; i < m_noiseGenerators.size(); ++i) {
                float noiseValue = m_noiseGenerators[i].getValue(
                    static_cast<float>(x),
                    static_cast<float>(y)
                );
                value += noiseValue * m_config.layers[i].weight;
                totalWeight += m_config.layers[i].weight;
            }

            heightmap.set(x, y, value / totalWeight);
        }
    }
}

void TerrainGenerator::applyIslandMask(Heightmap& heightmap) {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();
    const float centerX = w / 2.0f;
    const float centerY = h / 2.0f;
    const float maxDist = std::min(centerX, centerY);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float dx = (x - centerX) / maxDist;
            float dy = (y - centerY) / maxDist;
            float dist = std::sqrt(dx * dx + dy * dy);

            // Smooth falloff using power function
            float falloff = 1.0f - std::pow(clamp(dist, 0.0f, 1.0f), 1.0f / m_config.islandFalloff);
            falloff = clamp(falloff, 0.0f, 1.0f);

            heightmap.at(x, y) *= falloff;
        }
    }
}

void TerrainGenerator::generateContinentalMask(Heightmap& mask) {
    const auto& cfg = m_config.continentConfig;
    const size_t w = mask.width();
    const size_t h = mask.height();

    // Setup base continental noise generator
    NoiseConfig baseConfig;
    baseConfig.noiseType = NoiseConfig::Type::Simplex;
    baseConfig.fractalType = NoiseConfig::FractalType::FBm;
    baseConfig.frequency = cfg.continentFrequency;
    baseConfig.octaves = cfg.continentOctaves;
    baseConfig.persistence = cfg.continentPersistence;
    baseConfig.seed = m_config.seed;

    NoiseGenerator baseNoise(baseConfig);

    // Setup domain warp noise for coastline irregularity
    NoiseConfig warpConfig;
    warpConfig.noiseType = NoiseConfig::Type::Simplex;
    warpConfig.fractalType = NoiseConfig::FractalType::FBm;
    warpConfig.frequency = cfg.coastlineWarpFrequency;
    warpConfig.octaves = cfg.coastlineWarpOctaves;
    warpConfig.seed = m_config.seed + 500;

    NoiseGenerator warpNoiseX(warpConfig);
    warpConfig.seed = m_config.seed + 501;
    NoiseGenerator warpNoiseY(warpConfig);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // Apply domain warping for organic coastlines
            float warpX = warpNoiseX.getValue(fx, fy) * cfg.coastlineWarpAmp;
            float warpY = warpNoiseY.getValue(fx, fy) * cfg.coastlineWarpAmp;

            // Sample base continental noise with warped coordinates
            float value = baseNoise.getValue(fx + warpX, fy + warpY);

            mask.set(x, y, value);
        }
    }
}

float TerrainGenerator::calculateSeaLevel(const Heightmap& mask, float targetLandRatio) {
    // Build histogram of mask values
    const int numBins = 1000;
    std::vector<int> histogram(numBins, 0);

    float minVal = mask.min();
    float maxVal = mask.max();
    float range = maxVal - minVal;

    if (range < 0.0001f) {
        return minVal; // Flat mask, return minimum
    }

    const size_t totalPixels = mask.size();

    // Populate histogram
    for (size_t i = 0; i < totalPixels; ++i) {
        float v = mask.data()[i];
        int bin = static_cast<int>(((v - minVal) / range) * (numBins - 1));
        bin = clamp(bin, 0, numBins - 1);
        histogram[bin]++;
    }

    // Find threshold that gives target land ratio
    // Land is above threshold, so we count from top down
    size_t targetLandPixels = static_cast<size_t>(totalPixels * targetLandRatio);
    size_t landPixels = 0;

    for (int bin = numBins - 1; bin >= 0; --bin) {
        landPixels += histogram[bin];
        if (landPixels >= targetLandPixels) {
            // Found our threshold bin
            float threshold = minVal + (static_cast<float>(bin) / (numBins - 1)) * range;
            return threshold;
        }
    }

    return minVal; // Fallback
}

void TerrainGenerator::applyContinentalMask(Heightmap& heightmap, const Heightmap& mask, float seaLevel) {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();
    const float transitionWidth = m_config.continentConfig.transitionWidth;

    // Get mask value range for normalization
    float maskMin = mask.min();
    float maskMax = mask.max();
    float maskRange = maskMax - maskMin;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float maskValue = mask.get(x, y);
            float terrainValue = heightmap.get(x, y);

            // Normalize mask value and sea level to [0, 1] range
            float normalizedMask = (maskValue - maskMin) / maskRange;
            float normalizedSeaLevel = (seaLevel - maskMin) / maskRange;

            // Calculate distance from sea level
            float distFromSeaLevel = normalizedMask - normalizedSeaLevel;

            // Smooth transition using smoothstep
            float landFactor = smoothstep(-transitionWidth, transitionWidth, distFromSeaLevel);

            // Remap terrain values
            float oceanFloor = 0.2f;  // Ocean depth
            float landBase = 0.35f;   // Minimum land elevation

            float finalValue;
            if (landFactor < 0.01f) {
                // Deep ocean - subtle variation
                finalValue = oceanFloor * (0.7f + terrainValue * 0.3f);
            } else if (landFactor > 0.99f) {
                // Fully land - preserve terrain detail
                finalValue = landBase + terrainValue * (1.0f - landBase);
            } else {
                // Coastal transition
                float oceanValue = oceanFloor * (0.7f + terrainValue * 0.3f);
                float landValue = landBase + terrainValue * (1.0f - landBase);
                finalValue = lerp(oceanValue, landValue, landFactor);
            }

            heightmap.set(x, y, finalValue);
        }
    }
}

} // namespace worldgen
