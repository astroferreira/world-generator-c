#pragma once

#include "FastNoiseLite/FastNoiseLite.h"
#include "terrain/Heightmap.hpp"

namespace worldgen {

struct NoiseConfig {
    enum class Type { Perlin, Simplex, Cellular, Value };
    enum class FractalType { None, FBm, Ridged, PingPong };

    Type noiseType = Type::Simplex;
    FractalType fractalType = FractalType::FBm;
    int seed = 42;
    float frequency = 0.005f;
    int octaves = 6;
    float lacunarity = 2.0f;
    float persistence = 0.5f;
    float domainWarpAmp = 0.0f;
};

class NoiseGenerator {
public:
    NoiseGenerator();
    explicit NoiseGenerator(const NoiseConfig& config);

    void configure(const NoiseConfig& config);
    void setSeed(int seed);

    float getValue(float x, float y) const;
    void generate(Heightmap& heightmap) const;

private:
    mutable FastNoiseLite m_noise;
    mutable FastNoiseLite m_warp;
    NoiseConfig m_config;

    void applyConfig();
};

} // namespace worldgen
