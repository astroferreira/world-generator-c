#include "generation/NoiseGenerator.hpp"

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

NoiseGenerator::NoiseGenerator() {
    applyConfig();
}

NoiseGenerator::NoiseGenerator(const NoiseConfig& config)
    : m_config(config)
{
    applyConfig();
}

void NoiseGenerator::configure(const NoiseConfig& config) {
    m_config = config;
    applyConfig();
}

void NoiseGenerator::setSeed(int seed) {
    m_config.seed = seed;
    m_noise.SetSeed(seed);
    m_warp.SetSeed(seed + 1000);
}

float NoiseGenerator::getValue(float x, float y) const {
    float wx = x, wy = y;

    if (m_config.domainWarpAmp > 0.0f) {
        m_warp.DomainWarp(wx, wy);
    }

    // FastNoiseLite returns values in [-1, 1], normalize to [0, 1]
    float value = m_noise.GetNoise(wx, wy);
    return (value + 1.0f) * 0.5f;
}

void NoiseGenerator::generate(Heightmap& heightmap) const {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            heightmap.set(x, y, getValue(static_cast<float>(x), static_cast<float>(y)));
        }
    }
}

void NoiseGenerator::applyConfig() {
    // Set noise type
    switch (m_config.noiseType) {
        case NoiseConfig::Type::Perlin:
            m_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
            break;
        case NoiseConfig::Type::Simplex:
            m_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            break;
        case NoiseConfig::Type::Cellular:
            m_noise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
            break;
        case NoiseConfig::Type::Value:
            m_noise.SetNoiseType(FastNoiseLite::NoiseType_Value);
            break;
    }

    // Set fractal type
    switch (m_config.fractalType) {
        case NoiseConfig::FractalType::None:
            m_noise.SetFractalType(FastNoiseLite::FractalType_None);
            break;
        case NoiseConfig::FractalType::FBm:
            m_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
            break;
        case NoiseConfig::FractalType::Ridged:
            m_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
            break;
        case NoiseConfig::FractalType::PingPong:
            m_noise.SetFractalType(FastNoiseLite::FractalType_PingPong);
            break;
    }

    m_noise.SetSeed(m_config.seed);
    m_noise.SetFrequency(m_config.frequency);
    m_noise.SetFractalOctaves(m_config.octaves);
    m_noise.SetFractalLacunarity(m_config.lacunarity);
    m_noise.SetFractalGain(m_config.persistence);

    // Domain warp setup
    if (m_config.domainWarpAmp > 0.0f) {
        m_warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
        m_warp.SetDomainWarpAmp(m_config.domainWarpAmp);
        m_warp.SetSeed(m_config.seed + 1000);
        m_warp.SetFrequency(m_config.frequency * 0.5f);
    }
}

} // namespace worldgen
